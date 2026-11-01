#ifndef DY_SPHERE_ROLLER_H
#define DY_SPHERE_ROLLER_H

#include "logic/systems/system.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace dy {

// sphere_roll (docs/physical-agents/sphere-roller.md): a dynamic sphere steered by one torque vector, rewarded for
// matching a target velocity that drifts during the episode. One agent per world.

// The goal frame: y is world up, z the target direction (horizontal), x = up x z. In the engine's right-handed
// Y-up axes x points to the agent's left (Unity, left-handed, calls the same vector right). (x, y, z) is a proper
// rotation of the world, so torques and angular velocities keep their meaning in it
struct GoalFrame {
    glm::vec3 x, y, z;
    static GoalFrame fromForward(glm::vec3 forward);   // forward is projected on XZ
    glm::vec3 toLocal(glm::vec3 v) const { return glm::vec3(glm::dot(v, x), glm::dot(v, y), glm::dot(v, z)); }
    glm::vec3 toWorld(glm::vec3 v) const { return v.x * x + v.y * y + v.z * z; }
};

// Unit horizontal direction of a target heading (radians): (cos h, 0, sin h)
glm::vec3 headingDirection(float heading);

// The agent, next to the sphere's Transform and RigidBody: the task settings (JSON) and the episode state
struct SphereAgent {
    std::string behavior = "SphereRoller";
    float max_torque = 20.f;                    // N m for an action component of 1
    glm::vec2 speed_range = glm::vec2(0.5f, 3.f);   // target speeds, m/s ...
    float stop_chance = 0.1f;                   // ... or 0 with this probability
    float turn_rate = 0.7f;                     // rad/s of the target heading toward its goal
    float acceleration = 0.75f;                 // m/s^2 of the target speed toward its goal
    glm::vec2 goal_interval = glm::vec2(2.f, 6.f);  // seconds between goal draws
    float max_goal_turn = 2.1f;                 // rad: a new goal heading within this of the current one
    float home_radius = 30.f;                   // beyond it from the spawn point, new goal headings point back (+- 45 deg); 0: off
    float bounds = 55.f;                        // |x| or |z| beyond it truncates the episode; 0: off
    float fall_depth = 100.f;                   // this far below the spawn point truncates the episode
    float spawn_radius = 10.f;                  // spawn points within it of the spawn centre, on the ground
    float spawn_spacing = 0.f;                  // m: world i spawns around (i * spawn_spacing, 0) (vr_roller: two players apart)
    bool stagger = true;                        // the trainer's RESET starts each agent at a random point of its episode
    float reward_scale = 1.f;                   // r = exp(-|v_xz - v*| / reward_scale) per tick, m/s
    float probe_distance = 1.f;                 // horizontal distance of the look-ahead normals, m
    int max_decisions = 600;
    bool steered = false;                       // a player sets the goal (steerSphere, VRRoller): no goal draws, no time limit

    // Episode state
    glm::vec3 home = glm::vec3(0.f);            // the episode's spawn point
    float heading = 0.f;                        // the live target: heading (rad) and speed (m/s)
    float speed = 0.f;
    float goal_heading = 0.f;
    float goal_speed = 0.f;
    float goal_timer = 0.f;
    glm::vec3 decision_target = glm::vec3(0.f); // the target velocity of the last observation: rewards use it
    float decision_heading = 0.f;               // ... and its frame converts the action to a world torque
    bool has_decision = false;
    glm::vec3 torque = glm::vec3(0.f);          // world torque held over the window
    double reward_sum = 0.;
    double error_sum = 0.;
    int reward_ticks = 0;
    int decisions = 0;
    bool ended = false;
    bool out_of_bounds = false;
    bool pending_reset = false;                 // the terminal observation went out: new episode at the next window

    glm::vec3 targetVelocity() const { return speed * headingDirection(heading); }
};

// Observation layout (16 floats, every vector in the goal frame of the live target)
namespace SphereObs {
    constexpr int normal_below = 0;             // ground normal under the sphere
    constexpr int normal_velocity = 3;          // ground normal probe_distance ahead along the horizontal velocity
    constexpr int normal_target = 6;            // ground normal probe_distance ahead along the target direction
    constexpr int velocity = 9;                 // m/s
    constexpr int angular_velocity = 12;        // rad/s
    constexpr int target_speed = 15;            // m/s
    constexpr int size = 16;
}
constexpr int sphere_actions = 3;               // torque in the goal frame, x max_torque, clamped to [-1, 1]

// Per-tick reward: exp(-|v_xz - target| / scale)
float sphereReward(glm::vec3 velocity, glm::vec3 target, float scale);

// A player's command to a steered agent: the goal velocity (its horizontal part), which the live target turns and
// accelerates toward at turn_rate and acceleration. A null velocity keeps the heading. The command source picks
// the speeds: training's targets stop at speed_range.y
void steerSphere(SphereAgent& agent, glm::vec3 velocity);

// The speed a player's lean asks for (VRRoller): 0 up to `deadzone` (m), then logarithmic, every doubling of the
// lean adding the same speed, up to `top_speed` at `range` and beyond. Needs 0 < deadzone < range
float leanSpeed(float lean, float deadzone, float range, float top_speed);

// A proportional controller (tests, viewer): the torque that rolls the sphere toward the target velocity,
// up x (v* - v) in the goal frame, times gain, clamped
std::vector<float> sphereHeuristic(const std::vector<float>& observation, float gain = 1.f);

// New episode: spawn on the ground at rest, a fresh target, the AgentIO cleared
void resetSphereEpisode(entt::registry& reg);

// Before PhysicsStep, every tick: on the first tick of a window the pending reset and the action (as a world
// torque in the frame of the observed target); then the held torque
class SphereControlSys : public System {
public:
    SphereControlSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "SphereControl"; }
    void preinit() override;
    void init() override;
    void tick(double h) override;
};

// After PhysicsStep, every tick: the reward against the observed target, the target drift, the bounds
class SphereTaskSys : public System {
public:
    SphereTaskSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "SphereTask"; }
    void tick(double h) override;
};

// Observe phase: the window's reward and end of episode, then the observation of the live target, whose
// velocity becomes the decision target of the next window
class SphereObserveSys : public System {
public:
    SphereObserveSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "SphereObserve"; }
    void tick(double h) override;
};

// Act phase, the action slot of training templates: the training host writes AgentIO::action itself, so nothing
// to do. Viewer and eval scenes replace it (Policy, SphereHeuristic)
class TrainerActionsSys : public System {
public:
    TrainerActionsSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "TrainerActions"; }
    void tick(double h) override {}
};

// Act phase: sphereHeuristic on the last observation, for comparison with a policy
class SphereHeuristicSys : public System {
public:
    SphereHeuristicSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "SphereHeuristic"; }
    void tick(double h) override;
};

// The SphereAgent component and the systems above
void registerSphereRoller();

}

#endif
