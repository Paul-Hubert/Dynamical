#include "renderer.h"

#include <iostream>
#include "util/util.h"

#include "context/num_frames.h"

#include "extra/optick/optick.h"

Renderer::Renderer(entt::registry& reg) : reg(reg),
ctx(reg),
input(reg),
vr_input(reg),
ui(reg) {
    
    auto bindings = std::vector{
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1,
                                               vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
    };
    view_layout = ctx.device->createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo({}, (uint32_t)bindings.size(), bindings.data()));

    vr_render = std::make_unique<VRRender>(reg, ctx, view_layout);

    classic_render = std::make_unique<ClassicRender>(reg, ctx, view_layout);

    object_render = std::make_unique<ObjectRender>(reg, ctx, vr_render->getRenderpass(), std::vector<vk::DescriptorSetLayout>{view_layout});

    ui_render = std::make_unique<UIRender>(ctx, vr_render->getRenderpass());

}

void Renderer::preinit() {
    
}

void Renderer::init() {

}

void Renderer::prepare() {

    OPTICK_EVENT();

    frame_index = (frame_index + 1) % NUM_FRAMES;

    input.poll();

    vr_input.poll();

    ui.prepare();


    std::function<void(vk::CommandBuffer)> recorder = [&](vk::CommandBuffer command) {

        {

            OPTICK_GPU_EVENT("draw_objects");
            object_render->render(command, frame_index);

        }

        {

            OPTICK_GPU_EVENT("draw_ui");
            ui_render->render(command, frame_index);

        }

    };


    vr_render->prepare(frame_index, recorder, object_render->layout);

    classic_render->prepare(frame_index, recorder, object_render->layout);

    vr_input.update();

}

void Renderer::render() {
    
    OPTICK_EVENT();

    ctx.transfer.flush();
    
    ui.render();

    

    vr_render->render(frame_index);

    classic_render->render(frame_index);

}

void Renderer::finish() {

    ctx.device->waitIdle();

}

Renderer::~Renderer() {
    
}
