#ifndef DY_PHYSICS_SYS_H
#define DY_PHYSICS_SYS_H

#include "logic/systems/system.h"

#include <memory>

namespace dy {

class PhysicsWorld;
class TerrainColliders;

// Jolt rigid bodies at a fixed 60 Hz, Transforms interpolated between steps.
// Creates and destroys bodies with the RigidBody component, keeps terrain colliders around moving bodies.
class PhysicsSys : public System {
public:
    PhysicsSys(entt::registry& reg);
    ~PhysicsSys() override;
    const char* name() override { return "Physics"; }
    void preinit() override;
    void tick(double dt) override;
    void finish() override;

    static constexpr float fixed_dt = 1.f / 60.f;
    static constexpr int max_steps = 4;          // per frame, slower frames drop simulation time
    static constexpr float lookahead = 1.f;      // seconds of motion prefetched for terrain colliders
    static constexpr float kill_height = -1000.f;

private:
    void createBodies();
    void destroyBody(entt::registry& reg, entt::entity entity);
    void updateTerrain(int steps);

    PhysicsWorld* world = nullptr;      // registry context variable
    std::unique_ptr<TerrainColliders> terrain;

    double accumulator = 0.;
    double sim_time = 0.;
};

}

#endif
