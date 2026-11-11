#ifndef DY_ANIMAL_GAIT_WALK_H
#define DY_ANIMAL_GAIT_WALK_H

#include <glm/glm.hpp>

#include <memory>
#include <vector>

#include "logic/animals/gait.h"
#include "logic/animals/gait_dataset.h"
#include "logic/animals/gait_scene.h"

namespace dy {

class WorldRng;
class GroundField;

// What a walking animal is asked to do, and how that changes (docs/physical-agents/reports/animal-agents.md): a speed
// and a turn drawn every few seconds from GaitRanges, eased in over a second, and the heading that results. The same
// process drives the procedural walk of the demonstrations (the walker follows it exactly) and the task of the
// physical animal (the command of its observation and of its reward), so the two cannot be told apart by it
struct WalkCommand {
    float yaw = 0.f;                // heading asked for, radians about +y (0 looks down -z)
    float speed = 0.f;              // m/s along it
    float turn = 0.f;               // radians a metre, to the left
    float wanted_speed = 0.f, wanted_turn = 0.f;
    float until = 0.f;              // seconds at which the next draw happens
    float time = 0.f;
    bool started = false;
    float target_yaw = 0.f;         // on a terrain: the heading of the route drawn, turned to
    bool has_target = false;
    // Standing and running (GaitRanges::idle, run): speed alone says which, there is no other sign of the gait
    float run_speed = 0.f;          // m/s of the run clip; 0: the animal does not run, its draws are walks
    int only = 0;                   // 0: any draw; 1: no run (a walk of the demonstrations, which stands too); 2: runs alone
    int kind = 1;                   // what the last draw was: 0 stands, 1 walks, 2 runs

    glm::vec3 direction() const { return glm::vec3(-std::sin(yaw), 0.f, -std::cos(yaw)); }
    float yawRate() const { return turn * speed; }      // rad/s
};

// clip_speed : m/s of the animal's walk clip, length : of the animal (the radius of the turns is in lengths)
void drawCommand(WalkCommand& command, const GaitRanges& ranges, float clip_speed, float length, WorldRng& rng);
// A new walk : heading `yaw`, a first draw taken at once (no easing in). run_speed, only : WalkCommand's
void startCommand(WalkCommand& command, const GaitRanges& ranges, float clip_speed, float length, float yaw, WorldRng& rng,
    float run_speed = 0.f, int only = 0);
// 0 stands, 1 walks, 2 runs : what a speed asks of an animal whose clips walk at `walk_speed` and run at `run_speed`
// (0 : no run). Between the walk's fastest and the run's slowest order, the nearer of the two
int commandKind(const GaitRanges& ranges, float speed, float walk_speed, float run_speed);
// dt seconds later : a new draw when the last one is over, the speed and the turn eased toward it, the heading turned
void stepCommand(WalkCommand& command, const GaitRanges& ranges, float clip_speed, float length, WorldRng& rng, float dt);

// A style drawn from the ranges, lengths in metres (heights of the animal in the ranges). soft : -1 draws whether the
// ground is soft from ranges.soft, 0 never, 1 always
GaitStyle drawStyle(const GaitRanges& ranges, float height, WorldRng& rng, int soft = -1);

// One animal walking procedurally where a command sends it : what AnimalAnimation, AnimalWalkers and AnimalGait do
// for an entity, in one place and with no registry (the body generator, the demonstrations, tests)
struct GaitWalk {
    std::shared_ptr<const AnimalSpecies> species;
    GaitModel model;
    float length = 1.f;
    float sole = 0.f;
    Transform transform;
    Animal animal;
    AnimalGait gait;
    AnimalPose pose;                // pose.world : the joints in model space, as proceduralWalk left them
    AnimalWalker walker;
    std::vector<JointPose> scratch;

    // False when the clip cannot be read (model.problem)
    bool setup(std::shared_ptr<const AnimalSpecies> species, int clip, float length);
    // On the ground at `position`, heading `yaw`, at `phase` (0 to 1) of the cycle
    void start(const GaitStyle& style, glm::vec3 position, float yaw, float phase);
    void step(float speed, float turn, const GroundField* ground, float dt);
};

}

#endif
