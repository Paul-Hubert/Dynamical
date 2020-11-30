#include "renderer.h"

#include <iostream>
#include "util/util.h"

#include "context.h"

#include "logic/components/inputc.h"
#include "num_frames.h"

#include "optick.h"

Renderer::Renderer(entt::registry& reg) : reg(reg), ctx(std::make_unique<Context>(reg)), vr_render(reg, *ctx) {

}

void Renderer::preinit() {
    
    reg.set<Context*>(ctx.get());
    
}

void Renderer::init() {
    
}

void Renderer::render() {
    
    OPTICK_EVENT();

    ctx->transfer.flush();
    
    vr_render.render({}, {});

}

void Renderer::finish() {

    ctx->device->waitIdle();

}

Renderer::~Renderer() {
    
}
