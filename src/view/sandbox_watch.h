#ifndef DY_SANDBOX_WATCH_H
#define DY_SANDBOX_WATCH_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <string>

#include "logic/systems/system.h"

namespace dy {

// The desktop camera round an animal of the sandbox (docs/action-agents.md, scene sandbox_view): it takes a living
// animal of world 0, of the species asked for when there is one, and stays with it until it dies; then another.
// After PlayerControl and WorldStepper in the scene, which it overrides.
//
// params: "species" (id to prefer; empty: a predator first), "distance" (m from the animal, in its lengths), "height"
//         (in its heights), "orbit" (degrees a second)
class SandboxWatchSys : public System {
public:
    SandboxWatchSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "SandboxWatch"; }
    void tick(double dt) override;

private:
    std::string species;
    float distance = 3.5f, height = 2.f, orbit = 6.f;
    entt::entity target = entt::null;
    glm::vec3 focus = glm::vec3(0.f);
    bool has_focus = false;
    float angle = 0.6f;
};

}

#endif
