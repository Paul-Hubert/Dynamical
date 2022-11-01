#include "renderer.h"

#include <iostream>
#include "util/util.h"
#include "util/log.h"

#include "context/num_frames.h"

#include "extra/optick/optick.h"
#include "logic/settings/settings.h"

using namespace dy;

Renderer::Renderer(entt::registry& reg) : reg(reg),
ctx(reg),
per_frame(NUM_FRAMES) {
    
    auto& settings = reg.ctx<Settings>();
    
    for(int i = 0; i<NUM_FRAMES; i++) {
        per_frame[i].transfer_semaphore = ctx.device->createSemaphore({});
        per_frame[i].compute_semaphore = ctx.device->createSemaphore( {});
        per_frame[i].graphics_semaphore = ctx.device->createSemaphore( {});
    }

}

void Renderer::preinit() {
    
}

void Renderer::init() {

}

void Renderer::prepare() {

    OPTICK_EVENT();

    ctx.frame_index = (ctx.frame_index + 1) % NUM_FRAMES;
    
    auto& f = per_frame[ctx.frame_index];

    ctx.classic_render.prepare();

}

void Renderer::render() {
    
    OPTICK_EVENT();
    
    auto& f = per_frame[ctx.frame_index];

    std::vector<vk::Semaphore> waits{};
    std::vector<vk::PipelineStageFlags> stages{};
    std::vector<vk::Semaphore> signals{};

    if(last_frame_semaphore) {
        waits.push_back(last_frame_semaphore);
        stages.push_back(vk::PipelineStageFlagBits::eBottomOfPipe);
    }

    signals.push_back(f.transfer_semaphore);

    if(ctx.transfer.flush(waits, stages, signals)) {
        waits = signals;
        stages = {vk::PipelineStageFlagBits::eTransfer};
    }

    signals = {f.compute_semaphore};

    if(ctx.compute.flush(waits, stages, signals)) {
        waits = signals;
        stages = {vk::PipelineStageFlagBits::eComputeShader};
    }

    signals = {f.graphics_semaphore};

    ctx.classic_render.render(waits, stages, signals);

    last_frame_semaphore = f.graphics_semaphore;

}

void Renderer::finish() {

    ctx.device->waitIdle();

}

Renderer::~Renderer() {
    
    for(int i = 0; i<NUM_FRAMES; i++) {
        ctx.device->destroy(per_frame[i].transfer_semaphore);
        ctx.device->destroy(per_frame[i].compute_semaphore);
        ctx.device->destroy(per_frame[i].graphics_semaphore);
    }
    
}
