#include "renderer.h"

#include <iostream>
#include "util/util.h"

#include "context/context.h"
#include "input.h"
#include "vr_input.h"
#include "ui.h"
#include "vr_render/vr_render.h"

#include "optick.h"

Renderer::Renderer(entt::registry& reg) : reg(reg),
ctx(std::make_unique<Context>(reg)),
input(std::make_unique<Input>(reg)),
vr_input(std::make_unique<VRInput>(reg)),
ui(std::make_unique<UI>(reg)),
vr_render(std::make_unique<VRRender>(reg, *ctx)) {
    
}

void Renderer::preinit() {
    
}

void Renderer::init() {
    
}

void Renderer::update() {

    OPTICK_EVENT();

    input->poll();

    vr_input->poll();

    ui->prepare();

}

void Renderer::prepare() {

    OPTICK_EVENT();

    vr_render->prepare();

    vr_input->update();

}

void Renderer::render() {
    
    OPTICK_EVENT();

    ctx->transfer.flush();
    
    ui->render();

    vr_render->render({}, {});

}

void Renderer::finish() {

    ctx->device->waitIdle();

}

Renderer::~Renderer() {
    
}
