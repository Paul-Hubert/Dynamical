#ifndef DY_ANIMAL_VIEW_H
#define DY_ANIMAL_VIEW_H

#include "logic/systems/system.h"

#include <nlohmann/json.hpp>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <array>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "logic/animals/animal_body.h"

namespace dy {

// Host system of the physical animals' view scenes (docs/physical-agents/reports/animal-agents.md): the skinned mesh
// of the library follows the ragdoll of each world (joints that are bodies take their body's pose, the others ride
// on it as the animal stands), a camera follows one of them, a panel shows its command, speed and episode. The
// scene lists the "animals" pass and no AnimalAnimation: this system writes the poses.
// params: { "world": 0, "distance": 2.4 (in sizes of the animal), "height": 0.5 (in heights), "azimuth": 70 (degrees
//           from behind the commanded heading), "camera": true, "mesh": true, "spacing": 0 (as WorldView's),
//           "orders": [[seconds, speed, radius], ...] or the same as a string: the commands of the viewed animal, one
//                     after the other, instead of the draws (speed in speeds of the walk clip, radius of the turn in
//                     lengths of the animal: > 0 left, < 0 right, 0 straight; speed 0: drawn as in training). They
//                     are eased in as the draws are and start again with an episode,
//           "marks": 0 (m: a chequer of this size on the ground around the animal, to see it move),
//           "log": "" (a file: a line per frame, what tools/animal_orders_video.py writes on the film) }
class AnimalAgentViewSys : public System {
public:
    AnimalAgentViewSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "AnimalAgentView"; }
    void preinit() override;
    void tick(double dt) override;

private:
    struct Shown {
        entt::entity entity = entt::null;
        std::shared_ptr<const AnimalSpecies> species;
        AnimalBodyBind bind;
        bool failed = false;
    };
    int world = 0;
    float distance = 2.4f, height = 0.5f, azimuth = 70.f, spacing = 0.f;
    bool camera = true, mesh = true;
    std::vector<Shown> shown;
    glm::vec3 focus = glm::vec3(0.f);
    float view_heading = 0.f;
    bool has_focus = false;
    std::vector<std::array<float, 3>> orders;
    // The time of the orders runs on through a fall: the episode that follows takes the tour where it was
    float tour = 0.f, last_time = 0.f;
    float marks = 0.f;
    std::vector<entt::entity> tiles;
    std::string log_path;
    std::ofstream log;
};

}

#endif
