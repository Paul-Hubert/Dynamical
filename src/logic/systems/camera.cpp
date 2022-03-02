#include "system_list.h"

#include <math.h>

#include "glm/gtx/string_cast.hpp"

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
    camera.setSize(glm::vec2(width, width * ctx.swap.extent.height / ctx.swap.extent.width));
    //camera.setAngle(0);
    camera.setAngle(M_PI / 3);
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
    camera.setScreenSize(glm::vec2(ctx.swap.extent.width, ctx.swap.extent.width));
    
}

void CameraSys::finish() {
    
}

glm::mat4 Camera::createProjection() {
    return glm::ortho(-size.x/2, size.x/2, -size.y/2, +size.y/2, -1000.f, 1000.f);
}

glm::mat4 Camera::createView() {
    glm::mat4 camera = glm::identity<glm::mat4>();
    camera = glm::translate(camera, center);
    camera = glm::rotate(camera, angle, glm::vec3(1,0,0));
    return glm::inverse(camera);
}

glm::vec2 Camera::fromWorldSpace(glm::vec3 position) {
        return glm::project(position, getView(), getProjection(), glm::vec4(0, 0, screen_size.x, screen_size.y));
}

glm::vec3 Camera::fromScreenSpace(glm::vec2 screen_position) {
    glm::vec3 a = glm::unProject(glm::vec3(screen_position, 0), getView(), getProjection(), glm::vec4(0, 0, screen_size.x, screen_size.y));
    glm::vec3 b = glm::unProject(glm::vec3(screen_position, 1), getView(), getProjection(), glm::vec4(0, 0, screen_size.x, screen_size.y));
    // p = (b - a) * t + a
    // 0 = (b.z - a.z) * t + a.z
    // -a.z / (b.z - a.z) = t;
    float t = -a.z / (b.z - a.z);
    glm::vec3 p = (b - a) * t + a;
    return p;
}

