#include "renderer.h"

#include <iostream>
#include "util/util.h"

#include "context/num_frames.h"

#include "extra/optick/optick.h"
#include "logic/settings.h"

Renderer::Renderer(entt::registry& reg) : reg(reg),
ctx(reg),
ui(reg),
per_frame(NUM_FRAMES) {
    
    auto& settings = reg.ctx<Settings>();
    
    for(int i = 0; i<NUM_FRAMES; i++) {
        per_frame[i].transfer_semaphore = ctx.device->createSemaphore(vk::SemaphoreCreateInfo());
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

    ui.prepare();

    ctx.classic_render.prepare();

}

void Renderer::render() {
    
    OPTICK_EVENT();
    
    auto& f = per_frame[ctx.frame_index];
    
    vk::Semaphore s = f.transfer_semaphore;
    if(!ctx.transfer.flush(s)) {
        s = nullptr;
    }
    
    ctx.classic_render.render(s);

}

void Renderer::finish() {

    ctx.device->waitIdle();

}

Renderer::~Renderer() {
    
    for(int i = 0; i<NUM_FRAMES; i++) {
        ctx.device->destroy(per_frame[i].transfer_semaphore);
    }
    
}
