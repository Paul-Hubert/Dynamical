#ifndef DY_PHYSICS_SYS_H
#define DY_PHYSICS_SYS_H

#include "logic/systems/system.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>

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

// Creates the Jolt bodies of new RigidBody components (every tick), removes them with the component.
// Host (base) or world.
class RigidBodySys : public System {
public:
    RigidBodySys(entt::registry& reg) : System(reg) {}
    ~RigidBodySys() override;
    const char* name() override { return "RigidBody"; }
    void preinit() override;
    void tick(double dt) override;
    void finish() override;

private:
    void destroyBody(entt::registry& reg, entt::entity entity);

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
