#include "fixed_camera.h"

#include <cmath>

#include "logic/components/camera.h"

using namespace dy;

FixedCameraSys::FixedCameraSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    if(params.contains("pos")) pos = glm::vec3(params["pos"][0].get<float>(), params["pos"][1].get<float>(), params["pos"][2].get<float>());
    if(params.contains("look_at")) look_at = glm::vec3(params["look_at"][0].get<float>(), params["look_at"][1].get<float>(), params["look_at"][2].get<float>());
}

void FixedCameraSys::init() {
    auto& camera = reg.ctx().get<Camera>();
    camera.position = pos;
    glm::vec3 dir = glm::normalize(look_at - pos);
    camera.pitch = std::asin(glm::clamp(dir.y, -1.f, 1.f));
    camera.yaw = std::atan2(-dir.x, -dir.z);
}
