#include "sky_pass.h"

#include "renderer/context/context.h"
#include "renderer/util/pipeline.h"

using namespace dy;

SkyPass::SkyPass(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {
    layout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, ctx.view_layout));
}

void SkyPass::createPipeline(RenderTarget target, vk::RenderPass renderpass) {

    // One triangle over the screen, no vertex buffer
    GraphicsPipelineDesc desc;
    desc.vertex_shader = "./resources/shaders/sky.vert.glsl.spv";
    desc.fragment_shader = sceneFragmentShader(ctx, "sky");
    desc.layout = layout;
    desc.renderpass = renderpass;
    desc.cull = vk::CullModeFlagBits::eNone;
    desc.depth_test = false;
    desc.depth_write = false;

    pipelines[index(target)] = createGraphicsPipeline(ctx.device, desc);

}

void SkyPass::record(vk::CommandBuffer command, const RenderView& view) {
    command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines[index(view.target)]);
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {view.view_set}, {});
    command.draw(3, 1, 0, 0);
}

SkyPass::~SkyPass() {
    for(auto pipeline : pipelines) {
        if(pipeline) ctx.device->destroy(pipeline);
    }
    ctx.device->destroy(layout);
}
