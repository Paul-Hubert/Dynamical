#include "terrain_pass.h"

#include "renderer/context/context.h"
#include "renderer/terrain/soil_maps.h"
#include "renderer/terrain/terrain_sys.h"
#include "renderer/util/pipeline.h"
#include "renderer/util/texture.h"

#include "logic/components/render_settings.h"
#include "logic/settings/settings.h"
#include "logic/terrain/chunk.h"
#include "logic/terrain/ground_relief.h"
#include "logic/grass/grass_textures.h"

#include "util/log.h"
#include "util/paths.h"

#include <nlohmann/json.hpp>
#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>

using namespace dy;

// Layers of the ground textures : those of the soil maps (soil::Kind), then the rock of the steep faces
// and the earth under the grass
constexpr int ground_layers = 5;
const char* const ground_names[ground_layers] = { "mud", "stones", "sand", "rock", "earth" };

// The first ones have a relief the view ray is looked up in (logic/terrain/ground_relief.h) : how deep
// it is, in metres, from the top of the highest stone to the bottom of the texture
constexpr int relief_layers = 3;
const float relief_depths[relief_layers] = { 0.05f, 0.12f, 0.04f };

// Their mean colours (sRGB), drawn flat when resources/ground is not there
const uint8_t ground_flat[ground_layers][3] = { {78, 59, 39}, {113, 108, 96}, {166, 152, 133}, {108, 103, 93}, {107, 110, 71} };

// Push constants of terrain.frag.glsl
struct TerrainPC {
    glm::vec4 chunk;        // of each draw. xy : origin on the ground, z : layer of its soil map (< 0 : none), w : texels per metre
    float tiles[ground_layers];
    float smoothness[ground_layers];
    float under_grass;
    float library;
    float scree;
    float relief[relief_layers];    // depth of the relief of mud, stones and sand, in metres : 0 none
};

std::vector<vk::VertexInputBindingDescription> dy::terrainVertexBindings() {
    return {
        vk::VertexInputBindingDescription(0, sizeof(TerrainVertex), vk::VertexInputRate::eVertex),
    };
}

std::vector<vk::VertexInputAttributeDescription> dy::terrainVertexAttributes() {
    return {
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(TerrainVertex, pos)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32Sfloat, offsetof(TerrainVertex, normal)),
        vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32Sfloat, offsetof(TerrainVertex, curve)),
    };
}

TerrainPass::TerrainPass(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    auto poolSizes = std::vector {
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 5),
    };
    descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, poolSizes));

    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
    };
    materialLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));
    materialSet = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &materialLayout))[0];

    uint32_t width, height;
    texture = loadTextureArray(ctx, {"./resources/textures/grass.png", "./resources/textures/rock.jpg"}, vk::Format::eR8G8B8A8Srgb, width, height);

    textureView = ctx.device->createImageView(vk::ImageViewCreateInfo({}, texture->image, vk::ImageViewType::e2DArray, vk::Format::eR8G8B8A8Srgb,
        vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 2)));

    sampler = ctx.device->createSampler(vk::SamplerCreateInfo(
        {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat
    ));

    loadGround();

    // The ground is seen at grazing angles most of the time
    {
        vk::SamplerCreateInfo info(
            {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
            0.f, VK_FALSE, 1.f, VK_FALSE, vk::CompareOp::eNever, 0.f, VK_LOD_CLAMP_NONE
        );
        if(ctx.device.sampler_anisotropy) {
            info.anisotropyEnable = VK_TRUE;
            info.maxAnisotropy = std::min(8.f, ctx.device.physical.getProperties().limits.maxSamplerAnisotropy);
        }
        groundSampler = ctx.device->createSampler(info);
    }

    auto imageInfo = vk::DescriptorImageInfo(sampler, textureView, vk::ImageLayout::eShaderReadOnlyOptimal);
    auto colorInfo = vk::DescriptorImageInfo(groundSampler, groundColorView, vk::ImageLayout::eShaderReadOnlyOptimal);
    auto detailInfo = vk::DescriptorImageInfo(groundSampler, groundDetailView, vk::ImageLayout::eShaderReadOnlyOptimal);
    auto reliefInfo = vk::DescriptorImageInfo(groundSampler, reliefView, vk::ImageLayout::eShaderReadOnlyOptimal);
    auto reliefTopInfo = vk::DescriptorImageInfo(groundSampler, reliefTopView, vk::ImageLayout::eShaderReadOnlyOptimal);
    ctx.device->updateDescriptorSets({
        vk::WriteDescriptorSet(materialSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr, nullptr),
        vk::WriteDescriptorSet(materialSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &colorInfo, nullptr, nullptr),
        vk::WriteDescriptorSet(materialSet, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &detailInfo, nullptr, nullptr),
        vk::WriteDescriptorSet(materialSet, 3, 0, 1, vk::DescriptorType::eCombinedImageSampler, &reliefInfo, nullptr, nullptr),
        vk::WriteDescriptorSet(materialSet, 4, 0, 1, vk::DescriptorType::eCombinedImageSampler, &reliefTopInfo, nullptr, nullptr)
    }, {});

    auto layouts = std::vector { ctx.view_layout, materialLayout };
    auto pcRange = vk::PushConstantRange(vk::ShaderStageFlagBits::eFragment, 0, sizeof(TerrainPC));
    layout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, pcRange));

}

void TerrainPass::loadGround() {

    std::vector<TexturePixels> colors(ground_layers), details(ground_layers);

    try {

        auto directory = libraryResourceFile("ground");
        std::ifstream file(directory / "ground.json");
        if(!file) throw std::runtime_error("no " + (directory / "ground.json").string() + " (tools/ground_import.py writes it)");
        nlohmann::json manifest = nlohmann::json::parse(file);

        std::string color_files[ground_layers], detail_files[ground_layers];
        for(int layer = 0; layer < ground_layers; layer++) {
            const nlohmann::json* found = nullptr;
            for(const auto& entry : manifest.at("layers")) {
                if(entry.at("name").get<std::string>() == ground_names[layer]) found = &entry;
            }
            if(!found) throw std::runtime_error(std::string("no layer ") + ground_names[layer] + " in ground.json");
            groundTiles[layer] = 1.f / std::max(found->at("tile").get<float>(), 0.01f);
            groundSmoothness[layer] = found->value("smoothness", groundSmoothness[layer]);
            color_files[layer] = (directory / found->at("color").get<std::string>()).string();
            detail_files[layer] = (directory / found->at("detail").get<std::string>()).string();
        }

        // Eight images to decode and to filter down : on the workers
        std::exception_ptr failed;
        std::mutex failed_mutex;
        auto decode = [&](TexturePixels& out, const std::string& path, bool srgb) {
            try {
                out = decodeTextureMips(path, srgb);
            } catch(...) {
                std::lock_guard<std::mutex> lock(failed_mutex);
                failed = std::current_exception();
            }
        };
        tf::Taskflow taskflow;
        for(int layer = 0; layer < ground_layers; layer++) {
            taskflow.emplace([&, layer]() { decode(colors[layer], color_files[layer], true); });
            taskflow.emplace([&, layer]() { decode(details[layer], detail_files[layer], false); });
        }
        if(auto* executor = reg.ctx().find<tf::Executor>()) {
            executor->run(taskflow).wait();
        } else {
            tf::Executor local;
            local.run(taskflow).wait();
        }
        if(failed) std::rethrow_exception(failed);

        groundColor = uploadTextureArray(ctx, colors, vk::Format::eR8G8B8A8Srgb);
        groundDetail = uploadTextureArray(ctx, details, vk::Format::eR8G8B8A8Unorm);
        groundLibrary = true;
        loadRelief(&details);

    } catch(const std::exception& e) {

        dy::log(dy::warning) << "Ground textures : " << e.what() << ". The soil is drawn in flat colours\n";

        for(int layer = 0; layer < ground_layers; layer++) {
            colors[layer] = TexturePixels { 1, 1, { ground_flat[layer][0], ground_flat[layer][1], ground_flat[layer][2], 255 }, { 4 } };
            details[layer] = TexturePixels { 1, 1, { 128, 128, 128, 255 }, { 4 } };
        }
        groundColor = uploadTextureArray(ctx, colors, vk::Format::eR8G8B8A8Srgb);
        groundDetail = uploadTextureArray(ctx, details, vk::Format::eR8G8B8A8Unorm);
        groundLibrary = false;
        loadRelief(nullptr);

    }

    const uint32_t levels = (uint32_t) colors[0].level_sizes.size();
    groundColorView = ctx.device->createImageView(vk::ImageViewCreateInfo({}, groundColor->image, vk::ImageViewType::e2DArray, vk::Format::eR8G8B8A8Srgb,
        vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, levels, 0, ground_layers)));
    groundDetailView = ctx.device->createImageView(vk::ImageViewCreateInfo({}, groundDetail->image, vk::ImageViewType::e2DArray, vk::Format::eR8G8B8A8Unorm,
        vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, levels, 0, ground_layers)));

}

// The rays of the relief of a ground texture, from its file next to the other resources of the run
// directory, or baked and written there
GrassRaycast loadOrBakeRelief(const char* name, const std::vector<uint8_t>& heights, int width, int height) {

    GroundReliefParams params;
    const std::string file = std::string("./resources/ground_relief_") + name + ".bin";
    GrassRaycast data;
    if(loadGrassRaycast(data, file) && data.key == groundReliefKey(heights.data(), width, height, params)) {
        return data;
    }

    auto start = std::chrono::steady_clock::now();
    data = bakeGroundRelief(heights.data(), width, height, params);
    std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    dy::log(dy::info) << "Baked the relief of the " << name << " (" << data.size << " x " << data.size << " x " << data.slices() << ") in "
        << elapsed.count() << " s\n";

    if(!saveGrassRaycast(data, file)) {
        dy::log(dy::warning) << "Could not write " << file << ", the relief will be baked again next time\n";
    }
    return data;

}

void TerrainPass::loadRelief(const std::vector<TexturePixels>* details) {

    // Heights of the hits only : a layer per azimuth of each elevation, layer after layer of the soil
    std::vector<GrassTexture> oblique(relief_layers), top(relief_layers);
    uint32_t size = 1, slices = 1;
    bool compressed = false;

    if(details) {
        auto features = ctx.device.physical.getFormatProperties(vk::Format::eBc4UnormBlock).optimalTilingFeatures;
        compressed = (features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear) == vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
        for(int layer = 0; layer < relief_layers; layer++) {
            const TexturePixels& detail = (*details)[layer];
            std::vector<uint8_t> heights(size_t(detail.width) * detail.height);
            for(size_t i = 0; i < heights.size(); i++) heights[i] = detail.data[i * 4 + 2];
            GrassRaycast raycast = loadOrBakeRelief(ground_names[layer], heights, detail.width, detail.height);
            size = static_cast<uint32_t>(raycast.size);
            slices = static_cast<uint32_t>(raycast.slices());
            for(int e = 0; e < grass_elevations; e++) reliefLayout.angles[e] = raycast.angles[e];
            reliefLayout.low_from = raycast.low_from;
            reliefLayout.low_to = raycast.low_to;
            oblique[layer] = makeGrassTextures(raycast.oblique, raycast.size, raycast.slices(), 7, compressed).height;
            top[layer] = makeGrassTextures(raycast.top, raycast.size, 1, 7, compressed).height;
        }
        reliefBaked = true;
    } else {
        // Nothing to look up : a flat texel
        for(int layer = 0; layer < relief_layers; layer++) {
            oblique[layer] = GrassTexture { { 255 }, { 1 } };
            top[layer] = GrassTexture { { 255 }, { 1 } };
        }
        reliefBaked = false;
    }

    auto upload = [&](const std::vector<GrassTexture>& textures, uint32_t layers_each, std::shared_ptr<VmaImage>& image, vk::ImageView& view) {
        // The same level of every layer of the soil, one after the other
        GrassTexture merged;
        std::vector<size_t> offsets(textures.size(), 0);
        for(size_t level = 0; level < textures[0].level_sizes.size(); level++) {
            const size_t before = merged.data.size();
            for(size_t k = 0; k < textures.size(); k++) {
                merged.data.insert(merged.data.end(), textures[k].data.begin() + offsets[k], textures[k].data.begin() + offsets[k] + textures[k].level_sizes[level]);
                offsets[k] += textures[k].level_sizes[level];
            }
            merged.level_sizes.push_back(merged.data.size() - before);
        }
        const vk::Format format = compressed ? vk::Format::eBc4UnormBlock : vk::Format::eR8Unorm;
        const uint32_t levels = static_cast<uint32_t>(merged.level_sizes.size()), layers = layers_each * static_cast<uint32_t>(textures.size());
        vk::ImageCreateInfo info({}, vk::ImageType::e2D, format, vk::Extent3D(size, size, 1), levels, layers,
            vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
        image = ctx.transfer.createImageMips(merged.data.data(), merged.level_sizes, info, vk::ImageLayout::eShaderReadOnlyOptimal);
        view = ctx.device->createImageView(vk::ImageViewCreateInfo({}, image->image, vk::ImageViewType::e2DArray, format,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, levels, 0, layers)));
    };
    upload(oblique, slices, relief, reliefView);
    upload(top, 1, reliefTop, reliefTopView);

}

void TerrainPass::createPipeline(RenderTarget target, vk::RenderPass renderpass) {

    GraphicsPipelineDesc desc;
    desc.vertex_shader = "./resources/shaders/terrain.vert.glsl.spv";
    desc.fragment_shader = "./resources/shaders/terrain.frag.glsl.spv";
    desc.bindings = terrainVertexBindings();
    desc.attributes = terrainVertexAttributes();
    desc.layout = layout;
    desc.renderpass = renderpass;

    // How the rays of the relief are laid out : azimuths of each elevation (constant_id 0 to 3), heights
    // of the lowest rays (4 and 5)
    vk::SpecializationMapEntry entries[6];
    for(uint32_t e = 0; e < 4; e++) {
        entries[e] = vk::SpecializationMapEntry(e, offsetof(ReliefLayout, angles) + e * sizeof(int32_t), sizeof(int32_t));
    }
    entries[4] = vk::SpecializationMapEntry(4, offsetof(ReliefLayout, low_from), sizeof(float));
    entries[5] = vk::SpecializationMapEntry(5, offsetof(ReliefLayout, low_to), sizeof(float));
    auto specialization = vk::SpecializationInfo(6, entries, sizeof(ReliefLayout), &reliefLayout);
    desc.fragment_specialization = &specialization;

    pipelines[index(target)] = createGraphicsPipeline(ctx.device, desc);

}

void TerrainPass::record(vk::CommandBuffer command, const RenderView& view) {

    const entt::registry& creg = reg;

    auto* settings = creg.ctx().find<RenderSettings>();
    auto* buffers = creg.ctx().find<TerrainBuffers>();
    auto* soil = creg.ctx().find<SoilLayers>();
    if(!buffers || (settings && !settings->terrain)) return;

    command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines[index(view.target)]);
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {view.view_set, materialSet}, {});

    // Under the grass the ground is in its shade : what shows between the blades, and where the grass
    // pass puts its hit a little under the terrain, is dark
    TerrainPC pc {};
    pc.chunk = glm::vec4(0.f, 0.f, -1.f, 1.f / (float) chunk::cube_size);
    for(int layer = 0; layer < ground_layers; layer++) {
        pc.tiles[layer] = groundTiles[layer];
        pc.smoothness[layer] = groundSmoothness[layer];
    }
    pc.under_grass = settings && settings->grass && settings->grass_pass ? settings->grass_params.height : 0.f;
    pc.library = groundLibrary ? 1.f : 0.f;
    pc.scree = settings ? settings->scree : 1.f;
    auto* run = creg.ctx().find<Settings>();
    const float relief_scale = (settings ? settings->relief : 1.f) * (run ? run->ground_relief : 1.f);
    for(int layer = 0; layer < relief_layers; layer++) {
        pc.relief[layer] = reliefBaked ? relief_depths[layer] * relief_scale : 0.f;
    }
    command.pushConstants(layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(TerrainPC), &pc);

    for(auto [entity, position, gpu] : creg.view<const ChunkC, const ChunkGPU, const ChunkReady>().each()) {

        // Where the chunk is, for its soil map
        glm::vec3 origin = position.origin();
        glm::vec4 where(origin.x, origin.z, soil ? (float) soil->layer(gpu.slot, entity) : -1.f, pc.chunk.w);
        command.pushConstants(layout, vk::ShaderStageFlagBits::eFragment, offsetof(TerrainPC, chunk), sizeof(glm::vec4), &where);

        command.bindVertexBuffers(0, {buffers->vertices[gpu.slot]}, {0});
        command.drawIndirect(buffers->indirect, gpu.slot * buffers->indirect_stride, 1, 0);
    }

}

TerrainPass::~TerrainPass() {

    for(auto pipeline : pipelines) {
        if(pipeline) ctx.device->destroy(pipeline);
    }
    ctx.device->destroy(layout);
    ctx.device->destroy(groundSampler);
    ctx.device->destroy(reliefTopView);
    ctx.device->destroy(reliefView);
    ctx.device->destroy(groundDetailView);
    ctx.device->destroy(groundColorView);
    ctx.device->destroy(sampler);
    ctx.device->destroy(textureView);
    ctx.device->destroy(materialLayout);
    ctx.device->destroy(descPool);

}
