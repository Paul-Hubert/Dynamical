#ifndef DY_WORLD_H
#define DY_WORLD_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "logic/systems/system_set.h"
#include "physics/physics_world.h"
#include "scene_def.h"

namespace tf { class Executor; }

namespace dy {

// A world's fixed tick and decision window (simulation.md). Context variable of the world registry
struct WorldClock {
    double dt = 1. / 60.;
    uint32_t decision_period = 1;           // ticks per decision: physics_hz / control_hz
    uint64_t tick = 0;
};

// The world's random stream, seeded worldSeed(base_seed, index). Its own distributions: std::*_distribution
// output differs between standard libraries
class WorldRng {
public:
    explicit WorldRng(uint64_t seed = 0) : engine(seed) {}
    double uniform();                       // [0, 1)
    double uniform(double a, double b);
    double normal();
    glm::vec3 unitSphere();
    std::mt19937_64 engine;
};

struct WorldInfo {
    uint32_t index = 0;
    uint64_t seed = 0;
    std::string template_name;
};

uint64_t worldSeed(uint64_t base_seed, uint32_t index);

// A template's "physics" and "clock" blocks. Throw SceneError at the template's path
PhysicsConfig physicsConfig(const WorldDef& def);
WorldClock worldClock(const WorldDef& def);

// One independent simulation: its own registry, Jolt PhysicsSystem, clock, RNG and fixed-lane SystemSet
class World {
public:
    World(uint32_t index, uint64_t seed, const WorldDef& def);
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void preinit();
    void init();        // creates the template's entities, then the systems' init
    void tick();        // one fixed tick, with the Act / Observe phases of the decision window
    void finish();

    // Poses of every RigidBody body (determinism tests)
    uint64_t hash() const;

    uint32_t index;
    std::string template_name;
    entt::registry reg;
    SystemSet systems;

private:
    std::vector<EntityDef> entities;
    bool finished = false;
};

// The worlds of a scene, in index order. Host context variable in dynamical (WorldStepper, WorldView)
class Worlds {
public:
    Worlds() = default;
    Worlds(const Worlds&) = delete;
    Worlds& operator=(const Worlds&) = delete;

    // base_seed: the trainer's seed (dynamical_train) or 0
    void create(const SceneDef& scene, uint64_t base_seed);
    void preinit();
    void init();
    void finish();

    // Every world advances `ticks` fixed ticks; in parallel on the executor when given (one task per world)
    void step(int64_t ticks, tf::Executor* executor = nullptr);

    size_t size() const { return list.size(); }
    World& operator[](size_t i) { return *list[i]; }
    const World& operator[](size_t i) const { return *list[i]; }

    std::vector<std::unique_ptr<World>> list;
};

}

#endif
