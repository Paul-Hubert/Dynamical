#include "grass_pass.h"

#include "terrain_pass.h"
#include "raycast_data.h"

#include "renderer/context/context.h"
#include "renderer/terrain/terrain_sys.h"
#include "renderer/util/pipeline.h"

#include "logic/components/render_settings.h"

#include "util/log.h"
#include "util/tiled_noise.h"

#include "cereal/archives/portable_binary.hpp"

#include <algorithm>
#include <fstream>

using namespace dy;

constexpr int noise_size = 512;

struct GrassPC {
    glm::vec4 base_normal;
    float tile_size;
    float grass_height;
    float noise_frequency;
    float noise_amplitude;
};

GrassPass::GrassPass(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    auto poolSizes = std::vector {
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2),
    };
    descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, poolSizes));

    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
    };
    descLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));
    descSet = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &descLayout))[0];

    sampler = ctx.device->createSampler(vk::SamplerCreateInfo(
        {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat
    ));

    // Blade distance lookup : x, y position in the tile, z ray angle
    {
        auto raycast = std::make_unique<RaycastData>();
        std::ifstream is("./resources/raycast_data.bin", std::ios::binary);
        if(!is.good()) {
            dy::log(dy::error) << "resources/raycast_data.bin is missing, grass will look flat. Bake it with tools/raycast.\n";
        } else {
            cereal::PortableBinaryInputArchive in(is);
            in(*raycast);
        }

        vk::ImageCreateInfo info({}, vk::ImageType::e3D, vk::Format::eR8G8B8A8Unorm, vk::Extent3D(num_samples, num_samples, num_angles), 1, 1,
            vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
        raycastImage = ctx.transfer.createImage(raycast->data.data(), sizeof(Sample) * raycast->data.size(), info, vk::ImageLayout::eShaderReadOnlyOptimal);

        raycastView = ctx.device->createImageView(vk::ImageViewCreateInfo({}, raycastImage->image, vk::ImageViewType::e3D, vk::Format::eR8G8B8A8Unorm,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
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

    auto raycastInfo = vk::DescriptorImageInfo(sampler, raycastView, vk::ImageLayout::eShaderReadOnlyOptimal);
    auto noiseInfo = vk::DescriptorImageInfo(sampler, noiseView, vk::ImageLayout::eShaderReadOnlyOptimal);
    ctx.device->updateDescriptorSets({
        vk::WriteDescriptorSet(descSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &raycastInfo, nullptr, nullptr),
        vk::WriteDescriptorSet(descSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &noiseInfo, nullptr, nullptr)
    }, {});

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
    GrassPC pc { params.base_normal, params.tile_size, params.height, params.noise_frequency, params.noise_amplitude };

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
    ctx.device->destroy(raycastView);
    ctx.device->destroy(sampler);
    ctx.device->destroy(descLayout);
    ctx.device->destroy(descPool);

}
