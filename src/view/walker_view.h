#ifndef DY_WALKER_VIEW_H
#define DY_WALKER_VIEW_H

#include "logic/systems/system.h"

#include <nlohmann/json.hpp>

#include <glm/glm.hpp>

namespace dy {

// Host system of the walker scenes: a side camera following a world's walker, its target marker, a panel
// (checkpoint, command, height, episode). params: { "world": 0, "distance": 4.0, "height": 1.2, "side": 1.0, "camera": true }
class WalkerViewSys : public System {
public:
    WalkerViewSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "WalkerView"; }
    void tick(double dt) override;

private:
    int world = 0;
    float distance = 4.f, height = 1.2f, side = 1.f;
    bool camera = true;
    glm::vec3 focus = glm::vec3(0.f);
    bool has_focus = false;
};

}

#endif
