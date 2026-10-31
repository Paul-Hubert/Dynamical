#include "physics_sys.h"

#include "physics_world.h"
#include "terrain_colliders.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"

#include "scene/run_args.h"
#include "util/log.h"

#include <algorithm>
#include <chrono>

using namespace dy;

namespace {

// Last two simulated poses of a dynamic body, the Transform is interpolated between them
struct PhysicsPose {
    glm::vec3 prev_position;
    glm::vec3 position;
    glm::quat prev_rotation;
    glm::quat rotation;
};

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

}

PhysicsSys::PhysicsSys(entt::registry& reg) : System(reg) {

}

void PhysicsSys::preinit() {

    world = std::make_unique<PhysicsWorld>();
    terrain = std::make_unique<TerrainColliders>(reg, *world);

    reg.ctx().emplace<PhysicsStats>();
    reg.on_destroy<RigidBody>().connect<&PhysicsSys::destroyBody>(this);

}

void PhysicsSys::createBodies() {

    auto& bodies = world->bodies();
    std::vector<entt::entity> failed;

    for(auto [entity, transform, body] : reg.view<Transform, RigidBody>().each()) {

        if(body.created()) continue;

        JPH::ShapeSettings::ShapeResult result;
        if(body.shape == RigidBody::Shape::Sphere) {
            result = JPH::SphereShapeSettings(std::max(body.half_extent.x, 0.001f)).Create();
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

void PhysicsSys::destroyBody(entt::registry& reg, entt::entity entity) {

    auto& body = reg.get<RigidBody>(entity);
    JPH::BodyID id = bodyId(body);
    if(!body.created() || id.IsInvalid() || !world) return;

    world->bodies().RemoveBody(id);
    world->bodies().DestroyBody(id);
    body.body = JPH::BodyID().GetIndexAndSequenceNumber();

}

void PhysicsSys::updateTerrain(int steps) {

    // Bounds of the moving bodies over the coming steps, and further ahead for prefetching
    std::vector<Bounds> required, prefetch;

    const auto& lock_interface = world->system().GetBodyLockInterfaceNoLock();
    const float frame_time = steps * fixed_dt;

    for(auto [entity, body] : reg.view<RigidBody>().each()) {
        if(body.motion != RigidBody::Motion::Dynamic || !body.created()) continue;

        JPH::BodyLockRead lock(lock_interface, bodyId(body));
        if(!lock.Succeeded()) continue;

        const JPH::Body& b = lock.GetBody();
        JPH::AABox box = b.GetWorldSpaceBounds();
        Bounds bounds { toGlm(box.mMin), toGlm(box.mMax) };
        glm::vec3 velocity = toGlm(b.GetLinearVelocity());

        required.push_back(grow(bounds, velocity * frame_time, 1.f));
        prefetch.push_back(grow(bounds, velocity * lookahead, 4.f));
    }

    terrain->update(required, prefetch, sim_time);

}

void PhysicsSys::tick(double dt) {

    OPTICK_EVENT();

    auto& stats = reg.ctx().get<PhysicsStats>();
    auto& bodies = world->bodies();

    createBodies();

    accumulator += std::min(dt, 0.25);
    int steps = std::min((int) (accumulator / fixed_dt), max_steps);

    stats.steps_last_frame = steps;

    if(steps > 0) {

        auto start = clock::now();
        updateTerrain(steps);
        stats.colliders_ms = millis(start);

        start = clock::now();

        for(int s = 0; s < steps; s++) {

            // Kinematic bodies follow their Transform (VR hands), hidden ones (zero scale) stay put
            for(auto [entity, transform, body] : reg.view<Transform, RigidBody>().each()) {
                if(body.motion != RigidBody::Motion::Kinematic || !body.created() || transform.scale.x <= 0.f) continue;
                bodies.MoveKinematic(bodyId(body), JPH::RVec3(toJolt(transform.position)), toJolt(glm::normalize(transform.rotation)), fixed_dt);
            }

            world->step(fixed_dt);
            sim_time += fixed_dt;

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

        }

        stats.step_ms = millis(start);

        // Wall-clock timings are shown in the debug menu, keep fixed-clock captures reproducible
        if(auto* args = reg.ctx().find<RunArgs>(); args && args->deterministic()) {
            stats.step_ms = stats.colliders_ms = 0.f;
        }

        accumulator -= steps * fixed_dt;
        if(accumulator > fixed_dt) {
            // Too slow to keep up : let the simulation fall behind instead of spiraling
            accumulator = 0.;
        }

    }

    // Render between the last two steps
    float alpha = (float) (accumulator / fixed_dt);
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
    stats.terrain_cells = terrain->activeCells();
    stats.terrain_triangles = terrain->triangles();

}

void PhysicsSys::finish() {

    reg.on_destroy<RigidBody>().disconnect(this);

    for(auto [entity, body] : reg.view<RigidBody>().each()) {
        if(body.created() && !bodyId(body).IsInvalid()) {
            world->bodies().RemoveBody(bodyId(body));
            world->bodies().DestroyBody(bodyId(body));
        }
        body.body = JPH::BodyID().GetIndexAndSequenceNumber();
    }

    terrain->clear();

}

PhysicsSys::~PhysicsSys() {

    reg.on_destroy<RigidBody>().disconnect(this);

    terrain.reset();
    world.reset();

}
