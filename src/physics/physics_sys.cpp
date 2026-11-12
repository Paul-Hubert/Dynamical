#include "physics_sys.h"

#include "height_field.h"
#include "physics_world.h"
#include "terrain_colliders.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/terrain/terrain_generator.h"

#include "scene/run_args.h"
#include "scene/world.h"
#include "util/log.h"

#include <algorithm>
#include <chrono>

using namespace dy;

namespace {

JPH::BodyID bodyId(const RigidBody& body) {
    return JPH::BodyID(body.body);
}

Bounds grow(Bounds bounds, glm::vec3 motion, float margin) {
    Bounds out { glm::min(bounds.min, bounds.min + motion), glm::max(bounds.max, bounds.max + motion) };
    out.min -= glm::vec3(margin);
    out.max += glm::vec3(margin);
    return out;
}

using clock = std::chrono::steady_clock;

float millis(clock::time_point start) {
    return std::chrono::duration<float, std::milli>(clock::now() - start).count();
}

// Wall-clock timings are shown in the debug menu: fixed-clock runs (captures, probes) report 0
bool deterministic(entt::registry& reg) {
    auto* args = reg.ctx().find<RunArgs>();
    return args && args->deterministic();
}

FixedStepClock& fixedStepClock(entt::registry& reg) {
    if(auto* c = reg.ctx().find<FixedStepClock>()) return *c;
    return reg.ctx().emplace<FixedStepClock>();
}

PhysicsStats& physicsStats(entt::registry& reg) {
    if(auto* s = reg.ctx().find<PhysicsStats>()) return *s;
    return reg.ctx().emplace<PhysicsStats>();
}

}

void FixedSteps::remove(const void* owner) {
    for(auto* list : { &before, &after }) {
        list->erase(std::remove_if(list->begin(), list->end(), [owner](const Step& s) { return s.owner == owner; }), list->end());
    }
}

FixedSteps& dy::fixedSteps(entt::registry& reg) {
    if(auto* s = reg.ctx().find<FixedSteps>()) return *s;
    return reg.ctx().emplace<FixedSteps>();
}

int FixedStepClock::stepsFor(double dt) const {
    return std::min((int) ((accumulator + std::min(dt, 0.25)) / fixed_dt), max_steps);
}



// RigidBody

void RigidBodySys::preinit() {

    world = &physicsWorld(reg);
    physicsStats(reg);
    reg.on_destroy<RigidBody>().connect<&RigidBodySys::destroyBody>(this);
    reg.on_destroy<Ground>().connect<&RigidBodySys::destroyGround>(this);
    reg.on_destroy<HeightField>().connect<&RigidBodySys::destroyHeightField>(this);

}

void RigidBodySys::init() {
    // Static geometry exists before the first tick: world systems place their bodies on it in init
    createGrounds();
}

void RigidBodySys::createGrounds() {

    bool added = false;
    for(auto [entity, ground] : reg.view<Ground>().each()) {
        if(ground.created()) continue;
        glm::vec3 half = glm::max(ground.size * 0.5f, glm::vec3(0.001f));
        JPH::BodyCreationSettings settings(new JPH::BoxShape(toJolt(half)), JPH::RVec3(0.f, ground.top_y - half.y, 0.f),
            JPH::Quat::sIdentity(), JPH::EMotionType::Static, PhysicsLayers::static_layer);
        settings.mFriction = ground.friction;
        settings.mRestitution = ground.restitution;
        JPH::BodyID id = world->bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        if(id.IsInvalid()) {
            dy::log(dy::warning) << "Physics : out of bodies for the ground\n";
            continue;
        }
        ground.body = id.GetIndexAndSequenceNumber();
        added = true;
    }

    for(auto [entity, field] : reg.view<HeightField>().each()) {
        if(field.created()) continue;
        if(field.wave_list.empty()) {
            uint64_t seed = field.seed;
            if(seed == 0) {
                if(auto* rng = reg.ctx().find<WorldRng>()) seed = rng->engine();
            }
            drawWaves(field, seed);
        }
        std::vector<float> heights = sampleHeights(field);
        const float half = field.size() * 0.5f;
        JPH::HeightFieldShapeSettings shape(heights.data(), JPH::Vec3(-half, 0.f, -half), JPH::Vec3(field.spacing, 1.f, field.spacing), (JPH::uint32) field.samples);
        auto result = shape.Create();
        if(result.HasError()) {
            dy::log(dy::warning) << "Physics : invalid height field, " << result.GetError().c_str() << "\n";
            continue;
        }
        JPH::BodyCreationSettings settings(result.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, PhysicsLayers::static_layer);
        settings.mFriction = field.friction;
        settings.mRestitution = field.restitution;
        JPH::BodyID id = world->bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        if(id.IsInvalid()) {
            dy::log(dy::warning) << "Physics : out of bodies for the height field\n";
            continue;
        }
        field.body = id.GetIndexAndSequenceNumber();
        added = true;
    }

    // Static geometry is in place: rebuild the broad phase once
    if(added) world->system().OptimizeBroadPhase();

}

void RigidBodySys::destroyGround(entt::registry& reg, entt::entity entity) {
    auto& ground = reg.get<Ground>(entity);
    if(!ground.created() || !world) return;
    world->bodies().RemoveBody(JPH::BodyID(ground.body));
    world->bodies().DestroyBody(JPH::BodyID(ground.body));
    ground.body = JPH::BodyID().GetIndexAndSequenceNumber();
}

void RigidBodySys::destroyHeightField(entt::registry& reg, entt::entity entity) {
    auto& field = reg.get<HeightField>(entity);
    if(!field.created() || !world) return;
    world->bodies().RemoveBody(JPH::BodyID(field.body));
    world->bodies().DestroyBody(JPH::BodyID(field.body));
    field.body = JPH::BodyID().GetIndexAndSequenceNumber();
}

void RigidBodySys::tick(double dt) {

    createGrounds();

    auto& bodies = world->bodies();
    std::vector<entt::entity> failed;

    for(auto [entity, transform, body] : reg.view<Transform, RigidBody>().each()) {

        if(body.created()) continue;

        JPH::ShapeSettings::ShapeResult result;
        if(body.shape == RigidBody::Shape::Sphere) {
            result = JPH::SphereShapeSettings(std::max(body.half_extent.x, 0.001f)).Create();
        } else if(body.shape == RigidBody::Shape::Capsule) {
            result = JPH::CapsuleShapeSettings(std::max(body.half_extent.y, 0.001f), std::max(body.half_extent.x, 0.001f)).Create();
        } else {
            glm::vec3 half = glm::max(body.half_extent, glm::vec3(0.001f));
            float convex_radius = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min(half.x, std::min(half.y, half.z)));
            result = JPH::BoxShapeSettings(toJolt(half), convex_radius).Create();
        }
        if(result.IsValid() && body.offset != glm::vec3(0.f)) {
            result = JPH::RotatedTranslatedShapeSettings(toJolt(body.offset), JPH::Quat::sIdentity(), result.Get()).Create();
        }
        if(result.HasError()) {
            dy::log(dy::warning) << "Physics : invalid shape, " << result.GetError().c_str() << "\n";
            failed.push_back(entity);
            continue;
        }

        JPH::EMotionType motion = JPH::EMotionType::Dynamic;
        JPH::ObjectLayer layer = PhysicsLayers::moving_layer;
        if(body.motion == RigidBody::Motion::Static) {
            motion = JPH::EMotionType::Static;
            layer = PhysicsLayers::static_layer;
        } else if(body.motion == RigidBody::Motion::Kinematic) {
            motion = JPH::EMotionType::Kinematic;
        }

        JPH::BodyCreationSettings settings(result.Get(), JPH::RVec3(toJolt(transform.position)), toJolt(glm::normalize(transform.rotation)), motion, layer);
        settings.mFriction = body.friction;
        settings.mRestitution = body.restitution;
        settings.mAngularDamping = body.angular_damping;
        settings.mAllowSleeping = world->config().allow_sleeping;
        // The entity of a body, for whoever meets it in a query (0: none)
        settings.mUserData = (JPH::uint64) entt::to_integral(entity) + 1;
        if(body.motion == RigidBody::Motion::Dynamic && body.lock_rotation) {
            settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
        }
        if(body.motion == RigidBody::Motion::Dynamic && body.mass > 0.f) {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = body.mass;
        }
        if(body.motion == RigidBody::Motion::Dynamic) {
            settings.mLinearVelocity = toJolt(body.initial_velocity);
            settings.mAngularVelocity = toJolt(body.initial_angular_velocity);
            // Thrown objects must not tunnel through the thin terrain triangles
            settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
            // Smooth sliding over the edges between terrain triangles
            settings.mEnhancedInternalEdgeRemoval = true;
        }

        JPH::BodyID id = bodies.CreateAndAddBody(settings, motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        if(id.IsInvalid()) {
            dy::log(dy::warning) << "Physics : out of bodies\n";
            failed.push_back(entity);
            continue;
        }
        body.body = id.GetIndexAndSequenceNumber();

        if(body.motion == RigidBody::Motion::Dynamic) {
            reg.emplace_or_replace<PhysicsPose>(entity, transform.position, transform.position, transform.rotation, transform.rotation);
        }

    }

    // Do not retry every frame
    for(auto entity : failed) {
        reg.remove<RigidBody>(entity);
    }

}

void RigidBodySys::destroyBody(entt::registry& reg, entt::entity entity) {

    auto& body = reg.get<RigidBody>(entity);
    JPH::BodyID id = bodyId(body);
    if(!body.created() || id.IsInvalid() || !world) return;

    world->bodies().RemoveBody(id);
    world->bodies().DestroyBody(id);
    body.body = JPH::BodyID().GetIndexAndSequenceNumber();

}

void RigidBodySys::finish() {

    reg.on_destroy<RigidBody>().disconnect(this);
    reg.on_destroy<Ground>().disconnect(this);
    reg.on_destroy<HeightField>().disconnect(this);

    for(auto [entity, field] : reg.view<HeightField>().each()) {
        if(field.created()) {
            world->bodies().RemoveBody(JPH::BodyID(field.body));
            world->bodies().DestroyBody(JPH::BodyID(field.body));
        }
        field.body = JPH::BodyID().GetIndexAndSequenceNumber();
    }

    for(auto [entity, ground] : reg.view<Ground>().each()) {
        if(ground.created()) {
            world->bodies().RemoveBody(JPH::BodyID(ground.body));
            world->bodies().DestroyBody(JPH::BodyID(ground.body));
        }
        ground.body = JPH::BodyID().GetIndexAndSequenceNumber();
    }

    for(auto [entity, body] : reg.view<RigidBody>().each()) {
        if(body.created() && !bodyId(body).IsInvalid()) {
            world->bodies().RemoveBody(bodyId(body));
            world->bodies().DestroyBody(bodyId(body));
        }
        body.body = JPH::BodyID().GetIndexAndSequenceNumber();
    }

}

RigidBodySys::~RigidBodySys() {
    reg.on_destroy<RigidBody>().disconnect(this);
    reg.on_destroy<Ground>().disconnect(this);
    reg.on_destroy<HeightField>().disconnect(this);
    world = nullptr;
}



// TerrainColliders

TerrainCollidersSys::TerrainCollidersSys(entt::registry& reg) : System(reg) {

}

void TerrainCollidersSys::preinit() {
    terrain = std::make_unique<TerrainColliders>(reg, physicsWorld(reg));
    physicsStats(reg);
}

void TerrainCollidersSys::tick(double dt) {

    auto& clock = fixedStepClock(reg);
    // Boxes asked for this frame (a player's legs)
    std::vector<Bounds> needs;
    if(auto* asked = reg.ctx().find<TerrainNeeds>()) {
        for(auto& [min, max] : asked->boxes) needs.push_back(Bounds { min, max });
        asked->boxes.clear();
    }

    int steps = clock.stepsFor(dt);
    if(steps <= 0) return;

    auto start = clock::now();

    // Bounds of the moving bodies over the coming steps, and further ahead for prefetching. No background
    // prefetch in fixed-clock runs: colliders are only built synchronously, when required
    std::vector<Bounds> required, prefetch;
    const bool fixed_clock = deterministic(reg);

    auto& world = physicsWorld(reg);
    const auto& lock_interface = world.system().GetBodyLockInterfaceNoLock();
    const float frame_time = steps * FixedStepClock::fixed_dt;

    for(auto [entity, body] : reg.view<RigidBody>().each()) {
        if(body.motion != RigidBody::Motion::Dynamic || !body.created()) continue;

        JPH::BodyLockRead lock(lock_interface, bodyId(body));
        if(!lock.Succeeded()) continue;

        const JPH::Body& b = lock.GetBody();
        JPH::AABox box = b.GetWorldSpaceBounds();
        Bounds bounds { toGlm(box.mMin), toGlm(box.mMax) };
        glm::vec3 velocity = toGlm(b.GetLinearVelocity());

        required.push_back(grow(bounds, velocity * frame_time, 1.f));
        if(!fixed_clock) prefetch.push_back(grow(bounds, velocity * lookahead, 4.f));
    }

    required.insert(required.end(), needs.begin(), needs.end());

    terrain->update(required, prefetch, clock.sim_time);

    auto& stats = physicsStats(reg);
    stats.colliders_ms = fixed_clock ? 0.f : millis(start);
    stats.terrain_cells = terrain->activeCells();
    stats.terrain_triangles = terrain->triangles();

}

void TerrainCollidersSys::finish() {
    terrain->clear();
}

TerrainCollidersSys::~TerrainCollidersSys() {
    terrain.reset();
}



// WorldTerrain

WorldTerrainSys::WorldTerrainSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    seed = params.value("seed", seed);
    margin = params.value("margin", margin);
    below = params.value("below", below);
}

void WorldTerrainSys::preinit() {
    reg.ctx().emplace<std::shared_ptr<TerrainGenerator>>(std::make_shared<TerrainGenerator>(seed));
    terrain = std::make_shared<TerrainColliders>(reg, physicsWorld(reg));
    reg.ctx().emplace<std::shared_ptr<TerrainColliders>>(terrain);
}

void WorldTerrainSys::tick(double h) {

    auto& world = physicsWorld(reg);
    const auto& clock = reg.ctx().get<WorldClock>();
    const auto& lock_interface = world.system().GetBodyLockInterfaceNoLock();

    std::vector<Bounds> required;
    for(auto [entity, body] : reg.view<RigidBody>().each()) {
        if(body.motion != RigidBody::Motion::Dynamic || !body.created()) continue;
        JPH::BodyLockRead lock(lock_interface, bodyId(body));
        if(!lock.Succeeded()) continue;
        const JPH::Body& b = lock.GetBody();
        JPH::AABox box = b.GetWorldSpaceBounds();
        Bounds bounds = grow(Bounds { toGlm(box.mMin), toGlm(box.mMax) }, toGlm(b.GetLinearVelocity()) * (float) h, margin);
        bounds.min.y -= below;
        required.push_back(bounds);
    }

    // A ragdoll: one box around its segments
    if(auto* users = reg.ctx().find<TerrainUsers>(); users && !users->bodies.empty()) {
        Bounds all { glm::vec3(1e30f), glm::vec3(-1e30f) };
        for(JPH::BodyID id : users->bodies) {
            JPH::BodyLockRead lock(lock_interface, id);
            if(!lock.Succeeded()) continue;
            const JPH::Body& b = lock.GetBody();
            JPH::AABox box = b.GetWorldSpaceBounds();
            Bounds bounds = grow(Bounds { toGlm(box.mMin), toGlm(box.mMax) }, toGlm(b.GetLinearVelocity()) * (float) h, margin);
            all.min = glm::min(all.min, bounds.min);
            all.max = glm::max(all.max, bounds.max);
        }
        if(all.min.x <= all.max.x) {
            all.min.y -= below;
            required.push_back(all);
        }
    }

    terrain->update(required, {}, clock.tick * clock.dt);

}

void WorldTerrainSys::finish() {
    terrain->clear();
}



// Physics (base)

void PhysicsSys::preinit() {
    world = &physicsWorld(reg);
    fixedStepClock(reg);
    physicsStats(reg);
}

void PhysicsSys::tick(double dt) {

    auto& stats = physicsStats(reg);
    auto& clock = fixedStepClock(reg);
    auto& bodies = world->bodies();
    const float fixed_dt = FixedStepClock::fixed_dt;

    int steps = clock.stepsFor(dt);
    clock.accumulator += std::min(dt, 0.25);

    stats.steps_last_frame = steps;
    DY_PLOT("Physics steps", (int64_t) steps);

    if(steps > 0) {

        auto start = clock::now();

        for(int s = 0; s < steps; s++) {

            DY_ZONE_N("Physics step");

            // Kinematic bodies follow their Transform (VR hands), hidden ones (zero scale) stay put
            for(auto [entity, transform, body] : reg.view<Transform, RigidBody>().each()) {
                if(body.motion != RigidBody::Motion::Kinematic || !body.created() || transform.scale.x <= 0.f) continue;
                bodies.MoveKinematic(bodyId(body), JPH::RVec3(toJolt(transform.position)), toJolt(glm::normalize(transform.rotation)), fixed_dt);
            }

            // What the host's systems do at the physics rate (a player's legs, the drives of its hands)
            auto* hooks = reg.ctx().find<FixedSteps>();
            if(hooks) {
                DY_ZONE_N("Fixed steps before");
                for(size_t i = 0; i < hooks->before.size(); i++) hooks->before[i].run(fixed_dt);
            }

            {
                DY_ZONE_N("Jolt update");
                world->step(fixed_dt);
            }
            clock.sim_time += fixed_dt;

            for(auto [entity, body, pose] : reg.view<RigidBody, PhysicsPose>().each()) {
                if(!body.created()) continue;
                JPH::RVec3 position;
                JPH::Quat rotation;
                bodies.GetPositionAndRotation(bodyId(body), position, rotation);
                pose.prev_position = pose.position;
                pose.prev_rotation = pose.rotation;
                pose.position = toGlm(JPH::Vec3(position));
                pose.rotation = toGlm(rotation);
            }

            if(hooks) {
                DY_ZONE_N("Fixed steps after");
                for(size_t i = 0; i < hooks->after.size(); i++) hooks->after[i].run(fixed_dt);
            }

        }

        stats.step_ms = deterministic(reg) ? 0.f : millis(start);

        clock.accumulator -= steps * fixed_dt;
        if(clock.accumulator > fixed_dt) {
            // Too slow to keep up : let the simulation fall behind instead of spiraling
            clock.accumulator = 0.;
        }

    }

    // Render between the last two steps
    float alpha = (float) (clock.accumulator / fixed_dt);
    std::vector<entt::entity> fallen;
    for(auto [entity, transform, pose] : reg.view<Transform, PhysicsPose>().each()) {
        transform.position = glm::mix(pose.prev_position, pose.position, alpha);
        transform.rotation = glm::slerp(pose.prev_rotation, pose.rotation, alpha);
        if(transform.position.y < kill_height) fallen.push_back(entity);
    }
    for(auto entity : fallen) {
        reg.destroy(entity);
    }

    stats.bodies = world->system().GetNumBodies();
    stats.active_bodies = world->system().GetNumActiveBodies(JPH::EBodyType::RigidBody);
    DY_PLOT("Active bodies", (int64_t) stats.active_bodies);

}



// PhysicsStep (worlds)

void PhysicsStepSys::preinit() {
    world = &physicsWorld(reg);
}

void PhysicsStepSys::tick(double h) {

    auto& bodies = world->bodies();
    const int substeps = std::max(1, world->config().substeps);
    const float step = (float) (h / substeps);

    for(auto [entity, transform, body] : reg.view<Transform, RigidBody>().each()) {
        if(body.motion != RigidBody::Motion::Kinematic || !body.created()) continue;
        bodies.MoveKinematic(bodyId(body), JPH::RVec3(toJolt(transform.position)), toJolt(glm::normalize(transform.rotation)), (float) h);
    }

    for(int s = 0; s < substeps; s++) {
        DY_ZONE_N("Jolt update");
        world->step(step);
    }

    for(auto [entity, transform, body, pose] : reg.view<Transform, RigidBody, PhysicsPose>().each()) {
        if(!body.created()) continue;
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(bodyId(body), position, rotation);
        pose.prev_position = pose.position;
        pose.prev_rotation = pose.rotation;
        pose.position = toGlm(JPH::Vec3(position));
        pose.rotation = toGlm(rotation);
        transform.position = pose.position;
        transform.rotation = pose.rotation;
    }

}
