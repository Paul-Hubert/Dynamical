#include "renderer.h"

#include <iostream>
#include "util/util.h"

#include "context/num_frames.h"

#include "extra/optick/optick.h"
#include "logic/settings.h"

Renderer::Renderer(entt::registry& reg) : reg(reg),
ctx(reg),
input(reg),
ui(reg) {
    
    auto& settings = reg.ctx<Settings>();
    
    auto bindings = std::vector{
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1,
                                               vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
    };
    view_layout = ctx.device->createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo({}, (uint32_t)bindings.size(), bindings.data()));

    classic_render = std::make_unique<ClassicRender>(reg, ctx, view_layout);

    ui_render = std::make_unique<UIRender>(ctx, classic_render->getRenderpass());

}

void Renderer::preinit() {
    
}

void Renderer::init() {

}

void Renderer::prepare() {

    OPTICK_EVENT();

    frame_index = (frame_index + 1) % NUM_FRAMES;

    input.poll();

    ui.prepare();


    

}

void Renderer::render() {
    
    OPTICK_EVENT();
    
    ui.render();
    
    std::function<void(vk::CommandBuffer)> recorder = [&](vk::CommandBuffer command) {

        {

            OPTICK_GPU_EVENT("draw_ui");
            ui_render->render(command, frame_index);

        }

    };

    classic_render->prepare(frame_index, recorder, ui_render->pipelineLayout);
    
    ctx.transfer.flush();

    classic_render->render(frame_index);

}

void Renderer::finish() {

    ctx.device->waitIdle();

}

Renderer::~Renderer() {
    
}
