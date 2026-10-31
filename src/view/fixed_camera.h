#ifndef DY_FIXED_CAMERA_H
#define DY_FIXED_CAMERA_H

#include "logic/systems/system.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace dy {

// Places the desktop camera once at init (after PlayerControl's spawn camera): params { "pos": [x, y, z], "look_at": [x, y, z] }
class FixedCameraSys : public System {
public:
    FixedCameraSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "FixedCamera"; }
    void init() override;
    void tick(double dt) override {}

private:
    glm::vec3 pos = glm::vec3(0.f, 10.f, 20.f);
    glm::vec3 look_at = glm::vec3(0.f);
};

}

#endif
