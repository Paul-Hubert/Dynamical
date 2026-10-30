#ifndef DY_PHYSICS_WORLD_H
#define DY_PHYSICS_WORLD_H

// Jolt.h must come before any other Jolt header
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>

namespace dy {

namespace PhysicsLayers {
    constexpr JPH::ObjectLayer static_layer = 0;   // terrain colliders, static bodies
    constexpr JPH::ObjectLayer moving_layer = 1;   // dynamic and kinematic bodies
    constexpr JPH::ObjectLayer count = 2;
}

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer((JPH::uint8) layer); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer.GetValue() == 0 ? "static" : "moving"; }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
        return layer == PhysicsLayers::moving_layer || bp.GetValue() == PhysicsLayers::moving_layer;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == PhysicsLayers::moving_layer || b == PhysicsLayers::moving_layer;
    }
};

// Jolt setup : global registration, allocators, job threads and the physics system
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void step(float dt);

    JPH::PhysicsSystem& system() { return *physics; }
    JPH::BodyInterface& bodies() { return physics->GetBodyInterface(); }

private:
    BroadPhaseLayers broadphase_layers;
    ObjectVsBroadPhaseFilter object_vs_broadphase;
    ObjectPairFilter object_pairs;

    std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
    std::unique_ptr<JPH::JobSystemThreadPool> job_system;
    std::unique_ptr<JPH::PhysicsSystem> physics;
};

inline JPH::Vec3 toJolt(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
inline JPH::Quat toJolt(glm::quat q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
inline glm::vec3 toGlm(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
inline glm::quat toGlm(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

}

#endif
