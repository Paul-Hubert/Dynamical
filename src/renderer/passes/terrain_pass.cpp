#include "terrain_pass.h"

#include "renderer/context/context.h"
#include "renderer/terrain/terrain_sys.h"
#include "renderer/util/pipeline.h"
#include "renderer/util/texture.h"

#include "logic/components/render_settings.h"

using namespace dy;

std::vector<vk::VertexInputBindingDescription> dy::terrainVertexBindings() {
    return {
        vk::VertexInputBindingDescription(0, sizeof(TerrainVertex), vk::VertexInputRate::eVertex),
    };
}

std::vector<vk::VertexInputAttributeDescription> dy::terrainVertexAttributes() {
    return {
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(TerrainVertex, pos)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(TerrainVertex, normal)),
        vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(TerrainVertex, uv)),
    };
}

TerrainPass::TerrainPass(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    auto poolSizes = std::vector {
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1),
    };
    descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, poolSizes));

    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
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

    auto imageInfo = vk::DescriptorImageInfo(sampler, textureView, vk::ImageLayout::eShaderReadOnlyOptimal);
    ctx.device->updateDescriptorSets({
        vk::WriteDescriptorSet(materialSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr, nullptr)
    }, {});

    auto layouts = std::vector { ctx.view_layout, materialLayout };
    auto pcRange = vk::PushConstantRange(vk::ShaderStageFlagBits::eFragment, 0, sizeof(float));
    layout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, pcRange));

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
    if(!buffers || (settings && !settings->terrain)) return;

    command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines[index(view.target)]);
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {view.view_set, materialSet}, {});

    // Under the grass the ground is in its shade : what shows between the blades, and where the grass
    // pass puts its hit a little under the terrain, is dark
    float under_grass = settings && settings->grass && settings->grass_pass ? settings->grass_params.height : 0.f;
    command.pushConstants(layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(float), &under_grass);

    for(auto [entity, gpu] : creg.view<const ChunkGPU, const ChunkReady>().each()) {
        command.bindVertexBuffers(0, {buffers->vertices[gpu.slot]}, {0});
        command.drawIndirect(buffers->indirect, gpu.slot * buffers->indirect_stride, 1, 0);
    }

}

TerrainPass::~TerrainPass() {

    for(auto pipeline : pipelines) {
        if(pipeline) ctx.device->destroy(pipeline);
    }
    ctx.device->destroy(layout);
    ctx.device->destroy(sampler);
    ctx.device->destroy(textureView);
    ctx.device->destroy(materialLayout);
    ctx.device->destroy(descPool);

}
