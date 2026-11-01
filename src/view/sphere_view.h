#ifndef DY_SPHERE_VIEW_H
#define DY_SPHERE_VIEW_H

#include "logic/systems/system.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

namespace dy {

// Host system of sphere_view (docs/physical-agents/sphere-roller.md): follows one world's sphere with the desktop
// camera, draws the target velocity (green) and the sphere's horizontal velocity (orange) as bars from the sphere,
// marks its rotation with three dots, and shows a panel (policy, target, speed, reward). After WorldStepper.
// params: { "world": 0, "distance": 5.0, "height": 2.6, "side": 0.5, "smoothing": 1.5, "arrow_scale": 0.6,
//           "camera": true (false: leaves the desktop camera alone), "bars": true }
class SphereViewSys : public System {
public:
    SphereViewSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "SphereView"; }
    void tick(double dt) override;
    void finish() override;

private:
    entt::entity part(int i);
    void place(int i, glm::vec3 position, glm::quat rotation, glm::vec3 scale, glm::vec4 color);

    int world = 0;
    float distance = 5.f;
    float height = 2.6f;
    float smoothing = 1.5f;
    float side = 0.5f;          // radians the camera swings to the side of the motion, so the bars show their length
    float arrow_scale = 0.6f;
    bool camera = true;         // false: another system owns the desktop camera (vr_roller's VRRoller), no Follow
    bool follow = true;
    bool bars = true;           // the target and velocity bars (panel checkbox)

    bool have_direction = false;
    glm::vec3 direction = glm::vec3(0.f, 0.f, -1.f);    // the camera's horizontal viewing direction, smoothed
    entt::entity parts[5] = { entt::null, entt::null, entt::null, entt::null, entt::null };
};

}

#endif
