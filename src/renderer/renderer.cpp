#include "renderer.h"

#include <iostream>
#include "util/util.h"

#include "context.h"

#include "logic/components/inputc.h"
#include "num_frames.h"

Renderer::Renderer(entt::registry& reg) : System(reg), ctx(std::make_unique<Context>(reg)), camera(reg, *ctx), vr_render(reg, *ctx), waitsems(NUM_FRAMES), signalsems(NUM_FRAMES), computesems(NUM_FRAMES) {
    
    for (int i = 0; i < waitsems.size(); i++) {
        waitsems[i] = ctx->device->createSemaphore({});
        signalsems[i] = ctx->device->createSemaphore({});
        computesems[i] = ctx->device->createSemaphore({});
    }

    vr_render.setup();
    
}

void Renderer::preinit() {
    
    reg.set<Context*>(ctx.get());
    
}

void Renderer::init() {
    
}

void Renderer::tick() {

    ctx->transfer.flush();
    
    InputC& input = reg.ctx<InputC>();
    if(!input.window_showing) return;

    ctx->frame_index = (ctx->frame_index+1)%NUM_FRAMES;
    ctx->frame_num++;

    camera.update();

    try {

        uint32_t index = ctx->swap.acquire(waitsems[ctx->frame_index]);

        vr_render.render(index, camera, {waitsems[ctx->frame_index]}, {signalsems[ctx->frame_index]});

        ctx->swap.present(signalsems[ctx->frame_index]);

    } catch(vk::OutOfDateKHRError&) {
        //resize();
    }
    
}

void Renderer::finish() {

    ctx->device->waitIdle();

}

Renderer::~Renderer() {

    vr_render.cleanup();
    
    for(int i = 0; i < waitsems.size(); i++) {
        ctx->device->destroy(waitsems[i]);
        ctx->device->destroy(signalsems[i]);
        ctx->device->destroy(computesems[i]);
    }
    
}
