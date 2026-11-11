#include "grass_pass.h"

#include "terrain_pass.h"

#include "renderer/context/context.h"
#include "renderer/terrain/terrain_sys.h"
#include "renderer/util/pipeline.h"

#include "logic/components/render_settings.h"
#include "logic/grass/grass_bake.h"
#include "logic/grass/grass_textures.h"

#include "util/log.h"
#include "util/tiled_noise.h"

#include <algorithm>
#include <chrono>
#include <vector>

using namespace dy;

constexpr int noise_size = 512;
constexpr int max_mips = 7;

// Baked rays, written next to the other resources of the run directory the first time they are needed
const char* const raycast_file = "./resources/grass_raycast.bin";

// Push constants of grass.vert.glsl and grass.frag.glsl
struct GrassPC {
    glm::vec4 base_normal;      // w : follow_terrain
    float tile_size;
    float grass_height;
    float patch_size;
    float patch_amount;
    float noise_frequency;
    float noise_amplitude;
    float wind_strength;
    float wind_speed;
    float top_start;
    int32_t layers;
    float layer_spread;
    float layer_patch;
    float undergrowth;
    float colour_variation;
    float dryness;
    float curve;
    float shadow;
    float canopy;
    float silhouette;
    float azimuth_dither;
};

namespace {

GrassRaycast loadOrBake() {

    GrassBakeParams params;
    GrassRaycast data;
    if(loadGrassRaycast(data, raycast_file) && data.key == grassBakeKey(params)) {
        return data;
    }

    auto start = std::chrono::steady_clock::now();
    data = bakeGrass(params);
    std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    dy::log(dy::info) << "Baked the grass rays (" << data.size << " x " << data.size << " x " << data.angles << ") in " << elapsed.count() << " s\n";

    if(!saveGrassRaycast(data, raycast_file)) {
        dy::log(dy::warning) << "Could not write " << raycast_file << ", the grass rays will be baked again next time\n";
    }
    return data;

}

// BC4 and BC5 textures, 3D ones too, that the sampler can filter
bool blocksSupported(vk::PhysicalDevice physical) {

    for(vk::Format format : {vk::Format::eBc4UnormBlock, vk::Format::eBc5UnormBlock}) {
        auto features = physical.getFormatProperties(format).optimalTilingFeatures;
        if(!(features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) return false;
        for(vk::ImageType type : {vk::ImageType::e2D, vk::ImageType::e3D}) {
            try {
                physical.getImageFormatProperties(format, type, vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, {});
            } catch(const vk::SystemError&) {
                return false;
            }
        }
    }
    return true;

}

}

GrassPass::GrassPass(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    reg.ctx().get<RenderSettings>().grass_pass = true;

    auto poolSizes = std::vector {
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 7),
    };
    descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, poolSizes));

    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(6, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(7, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
    };
    descLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));
    descSet = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &descLayout))[0];

    // Every axis repeats : the tile, and the azimuth of the 45 degree rays
    sampler = ctx.device->createSampler(vk::SamplerCreateInfo(
        {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
        0.f, VK_FALSE, 1.f, VK_FALSE, vk::CompareOp::eNever, 0.f, VK_LOD_CLAMP_NONE
    ));

    // Baked rays, with mips for the grass far away. The heights, which every layer reads, apart from the
    // normals and the blade ids, which only the closest hit reads; in blocks where the GPU has them
    {
        GrassRaycast raycast = loadOrBake();
        const uint32_t size = static_cast<uint32_t>(raycast.size), angles = static_cast<uint32_t>(raycast.angles);
        const bool compressed = blocksSupported(ctx.device.physical);
        const vk::Format one = compressed ? vk::Format::eBc4UnormBlock : vk::Format::eR8Unorm;
        const vk::Format two = compressed ? vk::Format::eBc5UnormBlock : vk::Format::eR8G8Unorm;

        auto upload = [&](const GrassTexture& texture, vk::Format format, uint32_t depth, bool volume, Baked& out) {
            const uint32_t levels = static_cast<uint32_t>(texture.level_sizes.size());
            vk::ImageCreateInfo info({}, volume ? vk::ImageType::e3D : vk::ImageType::e2D, format, vk::Extent3D(size, size, depth), levels, 1,
                vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
            out.image = ctx.transfer.createImageMips(texture.data.data(), texture.level_sizes, info, vk::ImageLayout::eShaderReadOnlyOptimal);
            out.view = ctx.device->createImageView(vk::ImageViewCreateInfo({}, out.image->image, volume ? vk::ImageViewType::e3D : vk::ImageViewType::e2D, format,
                vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, levels, 0, 1)));
        };

        auto start = std::chrono::steady_clock::now();
        GrassTextures oblique = makeGrassTextures(raycast.oblique, raycast.size, raycast.angles, max_mips, compressed);
        GrassTextures top = makeGrassTextures(raycast.top, raycast.size, 1, max_mips, compressed);
        std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        dy::log(dy::info) << "Grass rays : " << (oblique.bytes() + top.bytes()) / 1e6 << " MB" << (compressed ? " in blocks, made in " : ", made in ")
            << elapsed.count() << " s\n";

        upload(oblique.height, one, angles, true, baked[0]);
        upload(top.height, one, 1, false, baked[1]);
        upload(oblique.normal, two, angles, true, baked[2]);
        upload(oblique.id, one, angles, true, baked[3]);
        upload(top.normal, two, 1, false, baked[4]);
        upload(top.id, one, 1, false, baked[5]);
    }

    // Tileable noise to break the repetition of the tiles
    {
        TiledNoise noise;
        std::vector<int8_t> data(4 * noise_size * noise_size, 0);
        for(int x = 0; x < noise_size; x++) {
            for(int y = 0; y < noise_size; y++) {
                for(int c = 0; c < 3; c++) {
                    data[(x * noise_size + y) * 4 + c] = static_cast<int8_t>(std::clamp(noise.octaveNoise(x * 10. / noise_size, y * 10. / noise_size, c * 1000., 2) * 128., -127., 127.));
                }
            }
        }

        vk::ImageCreateInfo info({}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Snorm, vk::Extent3D(noise_size, noise_size, 1), 1, 1,
            vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
        noiseImage = ctx.transfer.createImage(data.data(), data.size(), info, vk::ImageLayout::eShaderReadOnlyOptimal);

        noiseView = ctx.device->createImageView(vk::ImageViewCreateInfo({}, noiseImage->image, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Snorm,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
    }

    // Bindings of grass.frag.glsl : the heights first, the normals and the ids after the noise
    const uint32_t baked_bindings[6] = {0, 1, 4, 5, 6, 7};
    std::array<vk::DescriptorImageInfo, 6> bakedInfos;
    std::vector<vk::WriteDescriptorSet> writes;
    for(size_t i = 0; i < baked.size(); i++) {
        bakedInfos[i] = vk::DescriptorImageInfo(sampler, baked[i].view, vk::ImageLayout::eShaderReadOnlyOptimal);
        writes.push_back(vk::WriteDescriptorSet(descSet, baked_bindings[i], 0, 1, vk::DescriptorType::eCombinedImageSampler, &bakedInfos[i], nullptr, nullptr));
    }
    auto noiseInfo = vk::DescriptorImageInfo(sampler, noiseView, vk::ImageLayout::eShaderReadOnlyOptimal);
    writes.push_back(vk::WriteDescriptorSet(descSet, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &noiseInfo, nullptr, nullptr));
    ctx.device->updateDescriptorSets(writes, {});

    auto layouts = std::vector { ctx.view_layout, descLayout };
    auto pcRange = vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(GrassPC));
    layout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, pcRange));

}

void GrassPass::createPipeline(RenderTarget target, vk::RenderPass renderpass) {

    GraphicsPipelineDesc desc;
    desc.vertex_shader = "./resources/shaders/grass.vert.glsl.spv";
    desc.fragment_shader = "./resources/shaders/grass.frag.glsl.spv";
    desc.bindings = terrainVertexBindings();
    desc.attributes = terrainVertexAttributes();
    desc.layout = layout;
    desc.renderpass = renderpass;

    pipelines[index(target)] = createGraphicsPipeline(ctx.device, desc);

}

void GrassPass::record(vk::CommandBuffer command, const RenderView& view) {

    const entt::registry& creg = reg;

    auto* settings = creg.ctx().find<RenderSettings>();
    auto* buffers = creg.ctx().find<TerrainBuffers>();
    if(!buffers || !settings || !settings->grass) return;

    auto& params = settings->grass_params;
    GrassPC pc {
        glm::vec4(params.base_normal, params.follow_terrain),
        std::max(params.tile_size, 1e-3f), std::max(params.height, 1e-3f),
        std::max(params.patch_size, 1e-3f), params.patch_amount,
        params.noise_frequency, params.noise_amplitude,
        params.wind_strength, params.wind_speed,
        params.top_start, std::clamp(params.layers, 1, 3),
        params.layer_spread, params.layer_patch, params.undergrowth,
        params.colour_variation, params.dryness, params.curve,
        params.shadow, params.canopy, params.silhouette, params.azimuth_dither
    };

    command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines[index(view.target)]);
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {view.view_set, descSet}, {});
    command.pushConstants(layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(GrassPC), &pc);

    for(auto [entity, gpu] : creg.view<const ChunkGPU, const ChunkReady>().each()) {
        command.bindVertexBuffers(0, {buffers->vertices[gpu.slot]}, {0});
        command.drawIndirect(buffers->indirect, gpu.slot * buffers->indirect_stride, 1, 0);
    }

}

GrassPass::~GrassPass() {

    for(auto pipeline : pipelines) {
        if(pipeline) ctx.device->destroy(pipeline);
    }
    ctx.device->destroy(layout);
    ctx.device->destroy(noiseView);
    for(auto& texture : baked) {
        ctx.device->destroy(texture.view);
    }
    ctx.device->destroy(sampler);
    ctx.device->destroy(descLayout);
    ctx.device->destroy(descPool);

}
