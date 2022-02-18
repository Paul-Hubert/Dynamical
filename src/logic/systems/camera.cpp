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
    camera.setCenter(glm::vec3(0,0,0));
    float width = 100.f;
    camera.setSize(glm::vec2(width, camera.getSize().x * ctx.swap.extent.height / ctx.swap.extent.width));
}

void CameraSys::init() {
    
}

void CameraSys::tick(float dt) {
    
    OPTICK_EVENT();
    
    auto& input = reg.ctx<Input>();
    
    auto& camera = reg.ctx<Camera>();

    glm::vec2 size = camera.getSize();
    glm::vec3 center = camera.getCenter();

    size.x *= 1 - 0.1 * input.mouseWheel.y;
    
    float speed = 20.0 * size.x / 100.0;
    
    if(input.on[Input::FORWARD]) {
        center.y -= speed * dt;
    } if(input.on[Input::BACKWARD]) {
        center.y += speed * dt;
    } if(input.on[Input::LEFT]) {
        center.x -= speed * dt;
    } if(input.on[Input::RIGHT]) {
        center.x += speed * dt;
    }
    
    
    auto& ctx = *reg.ctx<Context*>();
    size.y = size.x * ctx.swap.extent.height / ctx.swap.extent.width;

    camera.setCenter(center);
    camera.setSize(size);
    
}

void CameraSys::finish() {
    
}

glm::mat4 Camera::createProjection() {
    return glm::ortho(-size.x/2, size.x/2, -size.y/2, +size.y/2, -10.f, 10.f);
}

glm::mat4 Camera::createView() {
    glm::mat4 camera = glm::identity<glm::mat4>();
    camera = glm::translate(camera, center);
    return glm::inverse(camera);
}

