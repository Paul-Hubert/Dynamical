#ifndef DY_PHYSICS_WORLD_H
#define DY_PHYSICS_WORLD_H

// Jolt.h must come before any other Jolt header
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <optional>

namespace dy {

// Object layers (physics.md): agents collide with arena geometry and moving props, never with each other
// (self-collision inside a ragdoll is a group filter); sensors only see what moves. Animals of a sandbox
// (docs/action-agents.md) are ragdolls that do collide with each other: a wolf's head on a deer
namespace PhysicsLayers {
    constexpr JPH::ObjectLayer static_layer = 0;   // STATIC: terrain colliders, ground, static bodies
    constexpr JPH::ObjectLayer moving_layer = 1;   // MOVING: dynamic and kinematic props
    constexpr JPH::ObjectLayer agent_layer = 2;    // AGENT: ragdoll segments
    constexpr JPH::ObjectLayer sensor_layer = 3;   // SENSOR: triggers
    constexpr JPH::ObjectLayer animal_layer = 4;   // ANIMAL: ragdoll segments that collide with other ragdolls of the layer
    constexpr JPH::ObjectLayer count = 5;

    // Broad phase: NON_MOVING (static) and MOVING (everything else)
    constexpr JPH::uint8 non_moving_bp = 0;
    constexpr JPH::uint8 moving_bp = 1;

    constexpr bool collide(JPH::ObjectLayer a, JPH::ObjectLayer b) {
        if(a > b) return collide(b, a);
        switch(a) {
        case static_layer: return b == moving_layer || b == agent_layer || b == animal_layer;
        case moving_layer: return true;
        case agent_layer: return b == sensor_layer;
        case sensor_layer: return b == animal_layer;
        case animal_layer: return true;
        default: return false;
        }
    }
}

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(layer == PhysicsLayers::static_layer ? PhysicsLayers::non_moving_bp : PhysicsLayers::moving_bp);
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer.GetValue() == 0 ? "non_moving" : "moving"; }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
        // Static and sensor layers only query moving things; moving props and agents query both
        if(layer == PhysicsLayers::static_layer || layer == PhysicsLayers::sensor_layer) return bp.GetValue() == PhysicsLayers::moving_bp;
        return true;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return PhysicsLayers::collide(a, b);
    }
};

// How a contact combines the two bodies' friction or restitution. Jolt's default is the geometric mean
enum class Combine : uint8_t { Geometric, Max, Min };

struct PhysicsConfig {
    int threads = 0;                    // 1: JobSystemSingleThreaded; n > 1: a pool of n threads; 0: master's pool
    uint32_t max_bodies = 65536;
    uint32_t max_body_pairs = 65536;
    uint32_t max_contacts = 16384;
    uint32_t temp_bytes = 16 * 1024 * 1024;
    glm::vec3 gravity = glm::vec3(0.f, -9.81f, 0.f);
    int substeps = 1;                   // our own sub-stepping (PhysicsStep); Jolt's collision steps stay 1
    bool allow_sleeping = true;         // agent and test worlds: off (physics.md)
    Combine friction = Combine::Geometric;
    Combine restitution = Combine::Geometric;
    // JPH::PhysicsSettings overrides, Jolt's defaults when unset
    std::optional<int> velocity_steps, position_steps;
    std::optional<float> speculative_distance, penetration_slop;
};

// A Jolt physics system with its job system and temporary allocator, as a registry context variable
// (base: created by the first physics system with the default config; worlds: from the template). Needs a JoltRuntime
class PhysicsWorld {
public:
    explicit PhysicsWorld(const PhysicsConfig& config = {});
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void step(float dt);

    JPH::PhysicsSystem& system() { return *physics; }
    JPH::BodyInterface& bodies() { return physics->GetBodyInterface(); }

    const PhysicsConfig& config() const { return settings; }
    bool singleThreaded() const;

    // The first STATIC surface straight below `from` within `distance` (ground, height fields, terrain colliders).
    // Outside the step only: no locking
    struct GroundHit {
        glm::vec3 point;
        glm::vec3 normal;
    };
    std::optional<GroundHit> groundBelow(glm::vec3 from, float distance);
    // The first STATIC surface on the segment from `from` to `to` (camera collision)
    std::optional<GroundHit> castStatic(glm::vec3 from, glm::vec3 to);
    // Where a sphere of `radius` at `centre` touches STATIC geometry, counting surfaces up to `margin` away: the
    // deepest point (on a face, an edge or a vertex) and the normal from it to the centre. Outside the step only
    std::optional<GroundHit> sphereContact(glm::vec3 centre, float radius, float margin = 0.f);

private:
    BroadPhaseLayers broadphase_layers;
    ObjectVsBroadPhaseFilter object_vs_broadphase;
    ObjectPairFilter object_pairs;

    PhysicsConfig settings;
    std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
    std::unique_ptr<JPH::JobSystem> job_system;
    bool single_threaded = false;      // JobSystemSingleThreaded (Jolt has no RTTI to ask)
    std::unique_ptr<JPH::PhysicsSystem> physics;
};

// The registry's PhysicsWorld, created with the default config (master's) when the registry has none yet
inline PhysicsWorld& physicsWorld(entt::registry& reg) {
    if(auto* world = reg.ctx().find<PhysicsWorld>()) return *world;
    return reg.ctx().emplace<PhysicsWorld>();
}

inline JPH::Vec3 toJolt(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
inline JPH::Quat toJolt(glm::quat q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
inline glm::vec3 toGlm(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
inline glm::quat toGlm(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

}

#endif
