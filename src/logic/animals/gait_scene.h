#ifndef DY_ANIMAL_GAIT_SCENE_H
#define DY_ANIMAL_GAIT_SCENE_H

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

#include "logic/animals/gait.h"
#include "logic/systems/system.h"

namespace dy {

class GroundField;
class TerrainGenerator;

// An animal that walks a path of its own : straight, a circle, a figure of eight or a weave. AnimalWalkersSys moves
// its Transform over the ground; with an AnimalGait next to it, the procedural walk makes its pose
struct AnimalWalker {
    enum class Path { Straight, Circle, Eight, Weave, Free };   // Free : `turn` is set by whoever drives it
    Path path = Path::Circle;
    float speed = 1.f;          // m/s
    float radius = 4.f;         // of the circle, of each loop of the eight, of the bends of the weave
    float turn = 0.f;           // radians a metre, to the left (Free)
    float yaw = 0.f;
    float travelled = 0.f;      // metres
    float height = 0.f;
    float pitch = 0.f;
    bool placed = false;
};

// How fast an AnimalWalker turns after `travelled` metres : radians per metre
float walkerCurvature(const AnimalWalker& walker);

// Moves a walker for dt seconds : along its path, on the ground, leaning with it
void stepWalker(AnimalWalker& walker, Transform& transform, float length, float sole, const GroundField* ground, float dt);

// Animals that walk side by side, each along its own path, to look at the procedural walk (scene animals_gait).
// Before AnimalAnimation and AnimalGait in the scene.
//
// params : "species" ([ids], default : a few animals that walk on four legs and on two),
//          "path" ("circle", "eight", "weave", "straight"), "radius" (in lengths of the animal, 2.5),
//          "speed" (1 : times the speed of the animal's walk clip), "gait" ("walk", "trot", "run"),
//          "style" (a GaitStyle : every animal walks with it), "procedural" (true; false : the clip as it is),
//          "origin" ([x, z]; without it, the most level ground within 400 m), "camera" (true), "gap" (metres, 3),
//          "follow" (-1; n : the desktop camera goes round the n-th animal, close to it), "orbit" (degrees a second, 6)
class AnimalWalkersSys : public System {
public:
    AnimalWalkersSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "AnimalWalkers"; }
    void preinit() override;
    void init() override;
    void tick(double dt) override;

private:
    std::vector<std::string> ids;
    AnimalWalker::Path path = AnimalWalker::Path::Circle;
    float radius = 2.5f;
    float speed = 1.f;
    std::string gait = "walk";
    GaitStyle style;
    bool procedural = true;
    bool has_origin = false;
    glm::vec2 origin = glm::vec2(0.f);
    bool move_camera = true;
    float gap = 3.f;
    int follow = -1;
    float orbit = 6.f;
    float angle = 0.9f;
    std::vector<entt::entity> walkers;

    std::shared_ptr<GroundField> ground;
};

}

#endif
