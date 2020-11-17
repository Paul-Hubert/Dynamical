#include "renderer.h"

#include <iostream>
#include "util/util.h"

#include "logic/components/inputc.h"
#include "num_frames.h"
#include "logic/components/renderinfo.h"

Renderer::Renderer(entt::registry& reg) : System(reg), ctx(std::make_unique<Context>(reg)), camera(reg, *ctx), main_render(reg, *ctx), waitsems(NUM_FRAMES), signalsems(NUM_FRAMES), computesems(NUM_FRAMES) {
    
    for (int i = 0; i < waitsems.size(); i++) {
        waitsems[i] = ctx->device->createSemaphore({});
        signalsems[i] = ctx->device->createSemaphore({});
        computesems[i] = ctx->device->createSemaphore({});
    }

    main_render.setup();
    
}

void Renderer::preinit() {
    
    reg.set<SDL_Window*>(ctx->win);
    
    reg.set<Device*>(&ctx->device);
    
    auto& ri = reg.set<RenderInfo>();
    ri.frame_index = 0;
    ri.frame_num = 0;
    
}

void Renderer::init() {
    
}

void Renderer::tick() {
    
    auto& ri = reg.ctx<RenderInfo>();
    
    ri.frame_index = (ri.frame_index+1)%NUM_FRAMES;
    ri.frame_num++;
    
    InputC& input = reg.ctx<InputC>();
    if(input.on[Action::RESIZE]) {
        resize();
        input.on.set(Action::RESIZE, false);
    }
    
    try {
        
        ctx->transfer.flush();
        
        camera.update();
        
        uint32_t index = ctx->swap.acquire(waitsems[ri.frame_index]);
        
        main_render.render(index, camera, { waitsems[ri.frame_index]}, { signalsems[ri.frame_index]});
        
        ctx->swap.present(signalsems[ri.frame_index]);
        
    } catch(vk::OutOfDateKHRError&) {
        
        resize();
        
    }
    
}

void Renderer::finish() {

    ctx->device->waitIdle();

}

Renderer::~Renderer() {

    main_render.cleanup();
    
    for(int i = 0; i < waitsems.size(); i++) {
        ctx->device->destroy(waitsems[i]);
        ctx->device->destroy(signalsems[i]);
        ctx->device->destroy(computesems[i]);
    }
    
}

void Renderer::resize() {
    
    if(ctx->win.resize()) {
        
        ctx->device->waitIdle();
        
        main_render.cleanup();
        
        ctx->swap.cleanup();
        
        ctx->swap.setup();
        
        camera.setup(ctx->swap.extent.width, ctx->swap.extent.height);
        
        main_render.setup();
    
    }
    
}
