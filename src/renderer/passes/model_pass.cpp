#include "model_pass.h"

#include "renderer/context/context.h"
#include "renderer/model/model.h"
#include "renderer/util/pipeline.h"
#include "renderer/util/texture.h"

#include "view/components/model_renderable.h"
#include "logic/components/render_settings.h"
#include "logic/components/transform.h"

using namespace dy;

constexpr uint32_t max_materials = 1024;

struct ModelPC {
    glm::mat4 model;
    glm::vec4 color;
};

ModelPass::ModelPass(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    reg.storage<ModelC>();
    reg.storage<ModelRenderable>();
    reg.storage<Transform>();

    auto poolSizes = std::vector {
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, max_materials),
    };
    descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, max_materials, poolSizes));

    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
    };
    materialLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));

    sampler = ctx.device->createSampler(vk::SamplerCreateInfo(
        {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat
    ));

    white = makeSolidTexture(ctx, 0xFFFFFFFF);

    auto layouts = std::vector { ctx.view_layout, materialLayout };
    auto pcRange = vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(ModelPC));
    layout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, pcRange));

}

void ModelPass::createPipeline(RenderTarget target, vk::RenderPass renderpass) {

    GraphicsPipelineDesc desc;
    desc.vertex_shader = "./resources/shaders/model.vert.glsl.spv";
    desc.fragment_shader = "./resources/shaders/model.frag.glsl.spv";
    desc.bindings = {
        vk::VertexInputBindingDescription(0, sizeof(ModelVertex), vk::VertexInputRate::eVertex),
    };
    desc.attributes = {
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(ModelVertex, position)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(ModelVertex, normal)),
        vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(ModelVertex, uv)),
    };
    desc.layout = layout;
    desc.renderpass = renderpass;

    pipelines[index(target)] = createGraphicsPipeline(ctx.device, desc);

}

vk::DescriptorSet ModelPass::makeMaterialSet(const std::shared_ptr<VmaImage>& image) {

    const auto& texture = image ? image : white;

    auto it = views.find(texture.get());
    if(it == views.end()) {
        vk::ImageView view = ctx.device->createImageView(vk::ImageViewCreateInfo({}, texture->image, vk::ImageViewType::e2D, texture->format,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
        it = views.emplace(texture.get(), std::make_pair(texture, view)).first;
    }

    vk::DescriptorSet set = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &materialLayout))[0];
    auto imageInfo = vk::DescriptorImageInfo(sampler, it->second.second, vk::ImageLayout::eShaderReadOnlyOptimal);
    ctx.device->updateDescriptorSets({
        vk::WriteDescriptorSet(set, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr, nullptr)
    }, {});

    return set;

}

void ModelPass::update(uint32_t frame_index) {

    // Descriptor sets of newly loaded models, on the main thread before the parallel recording
    for(auto [entity, model] : reg.view<ModelC>().each()) {
        for(auto& material : model.materials) {
            if(!material.set) {
                material.set = makeMaterialSet(material.image);
            }
        }
    }

}

void ModelPass::record(vk::CommandBuffer command, const RenderView& view) {

    const entt::registry& creg = reg;

    auto* settings = creg.ctx().find<RenderSettings>();
    if(settings && !settings->models) return;

    command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines[index(view.target)]);
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {view.view_set}, {});

    for(auto [entity, transform, renderable] : creg.view<const Transform, const ModelRenderable>().each()) {

        const ModelC* model = creg.try_get<ModelC>(renderable.model);
        if(!model || !model->vertices || !model->indices) continue;

        command.bindVertexBuffers(0, {model->vertices->buffer}, {0});
        command.bindIndexBuffer(model->indices->buffer, 0, vk::IndexType::eUint32);

        glm::mat4 matrix = transform.matrix();

        for(auto& part : model->parts) {
            auto& material = model->materials[part.material];
            if(!material.set) continue;

            ModelPC pc { matrix, material.base_color * renderable.tint };
            command.pushConstants(layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(ModelPC), &pc);
            command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 1, {material.set}, {});
            command.drawIndexed(part.index_count, 1, part.first_index, 0, 0);
        }
    }

}

ModelPass::~ModelPass() {

    for(auto pipeline : pipelines) {
        if(pipeline) ctx.device->destroy(pipeline);
    }
    for(auto& [image, entry] : views) {
        ctx.device->destroy(entry.second);
    }
    ctx.device->destroy(layout);
    ctx.device->destroy(sampler);
    ctx.device->destroy(materialLayout);
    ctx.device->destroy(descPool);

}
