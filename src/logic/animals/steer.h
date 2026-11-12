#ifndef DY_ANIMAL_STEER_H
#define DY_ANIMAL_STEER_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <vector>

#include "logic/animals/animal_body.h"
#include "logic/animals/gait_scene.h"
#include "logic/systems/system.h"

namespace dy {

class GroundField;

// An animal that goes where someone tells it (a rider: docs/players.md): a heading and a speed, nothing of who asks
// or of how the animal walks. While an animal has one, what moved it lets go of it (HerdSys) and takes it back when
// the component is gone. Two kinds of animals follow it:
//   - the kinematic ones (a Transform, an Animal with its clips and the procedural walk)     <- SteeredWalk
//   - the physical one of a world (AnimalBot: a ragdoll and its policy), through its proxy   <- AnimalBotSteer
struct AnimalSteer {
    float yaw = 0.f;            // heading asked for, radians about +y (0 looks down -z)
    float speed = 0.f;          // m/s along it; 0 stands
    bool run = false;           // the gait that goes with the speed: kinematic animals play their run clip (a physical
                                // one knows speeds alone)
};

// On a steered kinematic animal, written by SteeredWalkSys: its walk over the ground. It stays until the animal has
// come to a halt after its AnimalSteer went
struct SteeredWalk {
    AnimalWalker walker;        // Free: speed and turn are what the steering makes of them
    float acceleration = 0.f;   // m/s^2: the speed is eased to the one asked, its change too
    float yaw_rate = 0.f;       // rad/s
    float walk_speed = 0.f;     // m/s: the last walk asked of it
    bool running = false;       // it plays its run
};

// Entities with an AnimalSteer and a Transform (and no AnimalBotProxy) walk where they are steered, on the ground,
// turning at a limited rate; with an Animal they play their idle, their walk or their run by their speed (AnimalGait
// after it in the scene plants the feet). Before AnimalAnimation in the scene.
// params: { "response": 2.5 (1/s: how fast the speed follows), "turn": 2.2 (rad/s for an animal a metre long) }
class SteeredWalkSys : public System {
public:
    SteeredWalkSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "SteeredWalk"; }
    void preinit() override;
    void init() override;
    void tick(double dt) override;

private:
    float response = 2.5f;
    float turn = 2.2f;
    std::shared_ptr<GroundField> ground;
};

// The host's stand-in for the physical animal of a world (the scene's worlds are other registries, with their own
// physics): a Transform that is the animal's model space where its trunk stands (upright, heading as it does) and an
// AnimalPose of its joints there, for whoever meets the animal in the
// host (a rider). Made and moved by AnimalBotSteerSys
struct AnimalBotProxy {
    int world = 0;
    int species = -1;           // index in the library, -1: not one of it
    float walk_speed = 1.f;     // m/s of the slowest and of the fastest order its policy was trained with
    float top_speed = 1.f;
};

// One AnimalBotProxy per world that has an AnimalBot. A proxy with an AnimalSteer gives the animal its command (the
// WalkCommand of its observation: the speed asked, a turn toward the heading within what its ranges trained);
// without one the animal stands, or follows the draws of its task ("free": "wander"). After WorldStepper.
// params: { "free": "stand" | "wander" }
class AnimalBotSteerSys : public System {
public:
    AnimalBotSteerSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "AnimalBotSteer"; }
    void preinit() override;
    void tick(double dt) override;
    void finish() override;

private:
    bool wander = false;
    struct Skeleton {
        std::shared_ptr<const AnimalSpecies> species;
        AnimalBodyBind bind;
    };
    std::vector<entt::entity> proxies;      // per world
    std::vector<Skeleton> skeletons;
};

// On an Animal that is only the skin drawn on something else (the ragdoll of a world: AnimalAgentView), not an
// animal to meet
struct AnimalSkin {};

// The heading of a Transform: where its -z points, radians about +y
float headingOf(glm::quat rotation);

void registerAnimalSteer();

}

#endif
