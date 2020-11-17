#include "camera.h"

#include "context.h"
#include "swapchain.h"

#include "glm/gtc/matrix_transform.hpp"

#include "util/entt_util.h"

#include "logic/components/positionc.h"
#include "logic/components/camerac.h"

Camera::Camera(entt::registry& reg, Context& ctx) : reg(reg), ctx(ctx) {
    setup(ctx.swap.extent.width, ctx.swap.extent.height);
}

void Camera::setup(int width, int height) {
    
    projection = glm::perspective(render_fov, (float) width/height, render_min, render_distance);
    
}

void Camera::update() {
    
    auto player = reg.ctx<Util::Entity<"player"_hs>>();
    CameraC& cam = reg.get<CameraC>(player);
    auto pos = reg.get<PositionC>(player).pos;
    pos.y += 1.80f;
    
    view = glm::mat4(1.0);
    view = glm::translate(view, pos);
    view = glm::rotate(view, (float) cam.yAxis, glm::vec3(0.0f, 1.0f, 0.0f));
    view = glm::rotate(view, (float) cam.xAxis, glm::vec3(1.0f, 0.0f, 0.0f));
    view = glm::inverse(view);
    
    viewProjection = projection * view;
    
    position = pos;
    
}

glm::mat4 & Camera::getProjection() {
    return projection;
}

glm::mat4 & Camera::getView() {
    return view;
}

glm::mat4 & Camera::getViewProjection() {
    return viewProjection;
}

glm::vec3 Camera::getViewPosition() {
    return position;
}

Camera::~Camera() {
    
}
