#ifndef DY_SANDBOX_WATCH_H
#define DY_SANDBOX_WATCH_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "logic/systems/system.h"

namespace dy {

// The desktop camera round an animal of the sandbox (docs/action-agents.md, scene sandbox_view): it takes a living
// animal of world 0, of the species asked for when there is one, and stays with it until it dies; then another.
// After PlayerControl and WorldStepper in the scene, which it overrides.
//
// With "tour" it goes round the animals instead: close on one, turning round it for "stay" seconds, then it flies to
// the nearest animal of another species it has not just been to (a wolf, a deer, a wolf...), and so on. The flight
// takes 1.5 to 4 s by how far it is, pulls back and up on the way, turns to the next animal first and comes down
// into its close-up.
//
// params: "species" (id to prefer; empty: a predator first), "distance" (m from the animal, in its lengths), "height"
//         (in its heights), "orbit" (degrees a second), "tour", "stay" (s)
class SandboxWatchSys : public System {
public:
    SandboxWatchSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "SandboxWatch"; }
    void tick(double dt) override;

private:
    entt::entity pick(entt::registry& world, bool another) const;

    std::string species;
    float distance = 3.5f, height = 2.f, orbit = 6.f;
    bool tour = false;
    float stay = 5.f;

    entt::entity target = entt::null;
    int target_species = -1;
    glm::vec3 focus = glm::vec3(0.f);
    bool has_focus = false;
    float angle = 0.6f;
    // The tour: where the flight started, how long it takes, the time into the flight or the stay
    bool flying = false;
    glm::vec3 from = glm::vec3(0.f);
    float from_reach = 0.f, from_rise = 0.f, reach = 0.f, rise = 0.f, flight = 0.f, timer = 0.f;
    std::vector<entt::entity> visited;
};

}

#endif
