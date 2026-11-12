#ifndef DY_PHYSICS_SYS_H
#define DY_PHYSICS_SYS_H

#include "logic/systems/system.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace dy {

class PhysicsWorld;
class TerrainColliders;

// Last two simulated poses of a dynamic body: base interpolates its Transform between them, WorldView draws
// world bodies between them
struct PhysicsPose {
    glm::vec3 prev_position;
    glm::vec3 position;
    glm::quat prev_rotation;
    glm::quat rotation;
};

// Context variable of base's fixed-step accumulator, shared by TerrainColliders (which looks ahead at this
// frame's steps) and Physics (which takes them)
struct FixedStepClock {
    static constexpr float fixed_dt = 1.f / 60.f;
    static constexpr int max_steps = 4;          // per frame, slower frames drop simulation time

    double accumulator = 0.;
    double sim_time = 0.;

    // Steps the next Physics tick will take for a frame of dt
    int stepsFor(double dt) const;
};

// Context variable of base: what systems of the host do at the physics rate (forces, motor targets), called by
// Physics around each of its fixed steps, in the order the systems registered (their order in the scene). A system
// adds its step in preinit and takes it away in finish
struct FixedSteps {
    struct Step {
        const void* owner;
        std::function<void(float h)> run;
    };
    std::vector<Step> before;   // before each Jolt step
    std::vector<Step> after;    // after it, the poses of the step read

    void remove(const void* owner);
};

FixedSteps& fixedSteps(entt::registry& reg);

// Context variable of base: boxes of the world that need their terrain colliders this frame without a dynamic
// RigidBody in them (the ground under a player's legs). Filled every frame, emptied by TerrainColliders
struct TerrainNeeds {
    std::vector<std::pair<glm::vec3, glm::vec3>> boxes;     // min, max
};

// Creates the Jolt bodies of new RigidBody components (every tick), removes them with the component. Grounds and
// height fields are created in init and on later ticks. Host (base) or world.
class RigidBodySys : public System {
public:
    RigidBodySys(entt::registry& reg) : System(reg) {}
    ~RigidBodySys() override;
    const char* name() override { return "RigidBody"; }
    void preinit() override;
    void init() override;
    void tick(double dt) override;
    void finish() override;

private:
    void destroyBody(entt::registry& reg, entt::entity entity);
    void destroyGround(entt::registry& reg, entt::entity entity);
    void destroyHeightField(entt::registry& reg, entt::entity entity);
    void createGrounds();

    PhysicsWorld* world = nullptr;
};

// Marching cubes colliders around the moving bodies of base (host): cells the bodies reach this frame are built
// before the step, cells on their path are prefetched
class TerrainCollidersSys : public System {
public:
    TerrainCollidersSys(entt::registry& reg);
    ~TerrainCollidersSys() override;
    const char* name() override { return "TerrainColliders"; }
    void preinit() override;
    void tick(double dt) override;
    void finish() override;

    static constexpr float lookahead = 1.f;      // seconds of motion prefetched

private:
    std::unique_ptr<TerrainColliders> terrain;
};

// Marching cubes ground of a world (sphere_roll): TerrainColliders on the world's PhysicsWorld, built inline before
// the step around every dynamic body (its bounds grown by `margin`, and by `below` downward so the ground under a
// falling body is there). Shared as the world context variable std::shared_ptr<TerrainColliders>.
// params: { "seed": 1337, "margin": 2.0, "below": 6.0 } (base's terrain is seed 1337)
class WorldTerrainSys : public System {
public:
    WorldTerrainSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "WorldTerrain"; }
    void preinit() override;
    void tick(double h) override;
    void finish() override;

private:
    int seed = 1337;
    float margin = 2.f;
    float below = 6.f;
    std::shared_ptr<TerrainColliders> terrain;
};

// base's Jolt step (host): a fixed 60 Hz accumulator, Transforms interpolated between the last two steps,
// kinematic bodies following their Transform, fallen bodies removed
class PhysicsSys : public System {
public:
    PhysicsSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Physics"; }
    void preinit() override;
    void tick(double dt) override;

    static constexpr float kill_height = -1000.f;

private:
    PhysicsWorld* world = nullptr;
};

// One fixed tick of a world: PhysicsConfig::substeps Jolt updates of h / substeps, then the poses
class PhysicsStepSys : public System {
public:
    PhysicsStepSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "PhysicsStep"; }
    void preinit() override;
    void tick(double h) override;

private:
    PhysicsWorld* world = nullptr;
};

}

#endif
