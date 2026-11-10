#ifndef DY_WALKER_VIEW_H
#define DY_WALKER_VIEW_H

#include "logic/systems/system.h"

#include <nlohmann/json.hpp>

#include <glm/glm.hpp>

namespace dy {

// Host system of the walker scenes: a side camera following a world's walker, its target marker, a panel
// (checkpoint, command, height, episode). params: { "world": 0, "distance": 4.0, "height": 1.2, "side": 1.0, "camera": true }
// In a world with a ground field (walker_terrain_view) the camera follows as GaitView does: from behind the walker,
// swung "azimuth" degrees to its side, from the first place where the ground hides neither its feet nor its body
class WalkerViewSys : public System {
public:
    WalkerViewSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "WalkerView"; }
    void tick(double dt) override;

private:
    int world = 0;
    float distance = 4.f, height = 1.2f, side = 1.f, azimuth = 75.f;
    bool camera = true;
    glm::vec3 focus = glm::vec3(0.f), eye = glm::vec3(0.f);
    float view_heading = 0.f;
    int view_choice = 0;
    bool has_focus = false;
};

}

#endif
