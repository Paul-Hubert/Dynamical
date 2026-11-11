#ifndef DY_PURSUIT_H
#define DY_PURSUIT_H

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>

#include "logic/systems/system.h"

namespace dy {

struct AgentIO;

// pursuit (docs/action-agents.md, several agents in a world): Unity's Predator and Prey as points on a square field,
// no physics. The fixture of what the sandbox asks of the training host: agents of two behaviours in one world
// (components, not the world's AgentIO), a vector and a buffer observation, continuous and discrete actions, agents
// that die and are born with another id, and a behaviour that decides at half the rate of the other.
//
// Chasers catch the runners they come within catch_radius of: +1 for the chaser, -1 for the runner, who ends there;
// another runner is born somewhere else at the next window. A runner also ends of old age (a truncation), a
// chaser's episode by time: it starts again in place of itself with the same id. Both walk at `speed`, or sprint
// while their stamina lasts.

struct PursuitParams {
    float field = 20.f;             // half side of the square, m
    int chasers = 2, runners = 4;   // per world
    float catch_radius = 1.f;       // m
    float speed = 3.f;              // m/s for a speed action of 1
    float sprint = 1.7f;            // times that while sprinting
    float turn_rate = 3.f;          // rad/s for a turn action of 1
    float stamina_drain = 0.5f;     // of a full stamina, per second of sprint
    float stamina_gain = 0.15f;     // ... per second otherwise
    int chaser_decisions = 300;     // a chaser's episode
    int runner_decisions = 200;     // a runner's old age, in its own decisions
    int runner_every = 2;           // windows between two decisions of a runner (a chaser decides every window)
    float alive_reward = 0.01f;     // a runner's, per decision it is not caught in
    int others = 8;                 // rows of the buffer: the nearest ones
    bool runners_still = false;     // runners do not move, whatever they decide (a check of the chasers' learning)
    float approach_reward = 0.f;    // a chaser's, per metre it comes closer to the nearest runner (a check: dense where +1 a catch is sparse)
};

// Next to an AgentIO component. Headings are yaws about +y, 0 looks down -z, positive to the left
struct PursuitDot {
    bool chaser = false;
    glm::vec2 position = glm::vec2(0.f);    // x, z
    glm::vec2 velocity = glm::vec2(0.f);
    float yaw = 0.f;
    float stamina = 1.f;

    float speed = 0.f, turn = 0.f;          // the held action: 0..1 and -1..1
    bool sprint = false;

    int phase = 0;                          // a runner decides on windows where (window + phase) % runner_every == 0
    int decisions = 0;
    double reward = 0.;                     // since its last decision
    float nearest = -1.f;                   // a chaser: how far the nearest runner was at its last decision, < 0 none
    int catches = 0;                        // ...
    bool fresh = true;                      // no observation of this episode went out yet
    bool caught = false;
    bool ended = false;                     // its terminal step went out: leaves or starts again at the next window

    glm::vec2 forward() const;
    glm::vec2 left() const;
};

// World context variable
struct PursuitField {
    PursuitParams params;
    uint64_t windows = 0;
    uint64_t catches = 0, births = 0;
};

namespace PursuitObs {
    constexpr int position = 0;             // x, z over the field's half side
    constexpr int heading = 2;              // sin, cos of the yaw
    constexpr int speed = 4;                // over the sprint speed
    constexpr int stamina = 5;
    constexpr int size = 6;

    // A row of the buffer "Others": the nearest dots first, runners that were just caught left out
    constexpr int other_position = 0;       // forward, left, over the field's side
    constexpr int other_velocity = 2;       // relative, forward and left, over the sprint speed
    constexpr int other_chaser = 4;         // 1 or 0
    constexpr int other_runner = 5;
    constexpr int row = 6;
}
constexpr int pursuit_actions = 2;          // speed (-1..1 is 0 to `speed`), turn; one discrete branch: 0 walk, 1 sprint

// Toward the nearest runner (a chaser) or away from the nearest chaser (a runner), sprinting when close: writes the
// action of an agent from its observation (tests)
void pursuitHeuristic(AgentIO& io, bool chaser);

// Every tick. On the first tick of a window: ended runners leave and others are born, ended chasers start again,
// the actions are read. Then the dots move and chasers catch
//
// params: the fields of PursuitParams
class PursuitSys : public System {
public:
    PursuitSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "Pursuit"; }
    void preinit() override;
    void init() override;
    void tick(double h) override;

private:
    PursuitParams params;
};

// Observe phase: the reward, the end and the observation of every dot that is due
class PursuitObserveSys : public System {
public:
    PursuitObserveSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "PursuitObserve"; }
    void tick(double h) override;
};

void registerPursuit();

}

#endif
