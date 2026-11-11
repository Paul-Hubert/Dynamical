#include "terrain_pass.h"

#include "renderer/context/context.h"
#include "renderer/terrain/soil_maps.h"
#include "renderer/terrain/terrain_sys.h"
#include "renderer/util/pipeline.h"
#include "renderer/util/texture.h"

#include "logic/components/render_settings.h"
#include "logic/terrain/chunk.h"

#include "util/log.h"
#include "util/paths.h"

#include <nlohmann/json.hpp>
#include <taskflow/taskflow.hpp>

#include <algorithm>
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
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 3),
    };
    descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, poolSizes));

    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
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
    ctx.device->updateDescriptorSets({
        vk::WriteDescriptorSet(materialSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr, nullptr),
        vk::WriteDescriptorSet(materialSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &colorInfo, nullptr, nullptr),
        vk::WriteDescriptorSet(materialSet, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &detailInfo, nullptr, nullptr)
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

    } catch(const std::exception& e) {

        dy::log(dy::warning) << "Ground textures : " << e.what() << ". The soil is drawn in flat colours\n";

        for(int layer = 0; layer < ground_layers; layer++) {
            colors[layer] = TexturePixels { 1, 1, { ground_flat[layer][0], ground_flat[layer][1], ground_flat[layer][2], 255 }, { 4 } };
            details[layer] = TexturePixels { 1, 1, { 128, 128, 128, 255 }, { 4 } };
        }
        groundColor = uploadTextureArray(ctx, colors, vk::Format::eR8G8B8A8Srgb);
        groundDetail = uploadTextureArray(ctx, details, vk::Format::eR8G8B8A8Unorm);
        groundLibrary = false;

    }

    const uint32_t levels = (uint32_t) colors[0].level_sizes.size();
    groundColorView = ctx.device->createImageView(vk::ImageViewCreateInfo({}, groundColor->image, vk::ImageViewType::e2DArray, vk::Format::eR8G8B8A8Srgb,
        vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, levels, 0, ground_layers)));
    groundDetailView = ctx.device->createImageView(vk::ImageViewCreateInfo({}, groundDetail->image, vk::ImageViewType::e2DArray, vk::Format::eR8G8B8A8Unorm,
        vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, levels, 0, ground_layers)));

}

void TerrainPass::createPipeline(RenderTarget target, vk::RenderPass renderpass) {

    GraphicsPipelineDesc desc;
    desc.vertex_shader = "./resources/shaders/terrain.vert.glsl.spv";
    desc.fragment_shader = "./resources/shaders/terrain.frag.glsl.spv";
    desc.bindings = terrainVertexBindings();
    desc.attributes = terrainVertexAttributes();
    desc.layout = layout;
    desc.renderpass = renderpass;

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
    ctx.device->destroy(groundDetailView);
    ctx.device->destroy(groundColorView);
    ctx.device->destroy(sampler);
    ctx.device->destroy(textureView);
    ctx.device->destroy(materialLayout);
    ctx.device->destroy(descPool);

}
