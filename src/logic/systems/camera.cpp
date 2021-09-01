#include "system_list.h"

#include <math.h>

#include "logic/components/camera.h"
#include "renderer/context/context.h"
#include "logic/components/input.h"

#include "util/log.h"

using namespace dy;

void CameraSys::preinit() {
    
    auto& ctx = *reg.ctx<Context*>();
    
    auto& camera = reg.set<Camera>();
    camera.center = glm::ivec2(0,0);
    camera.corner = camera.center - camera.size / 2.f;
    camera.size.x = 100.0f;
    camera.size.y = camera.size.x * ctx.swap.extent.height / ctx.swap.extent.width;
}

void CameraSys::init() {
    
}

void CameraSys::tick(float dt) {
    
    auto& input = reg.ctx<Input>();
    
    auto& camera = reg.ctx<Camera>();
    
    camera.size.x *= 1 - 0.1 * input.mouseWheel.y;
    
    float speed = 20.0 * camera.size.x / 100.0;
    
    if(input.on[Input::FORWARD]) {
        camera.center.y -= speed * dt;
    } if(input.on[Input::BACKWARD]) {
        camera.center.y += speed * dt;
    } if(input.on[Input::LEFT]) {
        camera.center.x -= speed * dt;
    } if(input.on[Input::RIGHT]) {
        camera.center.x += speed * dt;
    }
    
    
    auto& ctx = *reg.ctx<Context*>();
    camera.size.y = camera.size.x * ctx.swap.extent.height / ctx.swap.extent.width;
    
    camera.corner = camera.center - camera.size / 2.f;
    
}

void CameraSys::finish() {
    
}
