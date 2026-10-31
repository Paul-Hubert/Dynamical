#include "physics_world.h"

#include "jolt_runtime.h"

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>

#include "util/log.h"

#include <algorithm>
#include <thread>

using namespace dy;

namespace {

float combine(Combine rule, float a, float b) {
    return rule == Combine::Max ? std::max(a, b) : std::min(a, b);
}

}

PhysicsWorld::PhysicsWorld(const PhysicsConfig& config) : settings(config) {

    if(!JoltRuntime::active()) dy::log(dy::critical) << "PhysicsWorld created without a JoltRuntime\n";

    temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(config.temp_bytes);

    if(config.threads == 1) {
        // Training worlds: one thread each, the worlds themselves run in parallel
        job_system = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
        single_threaded = true;
    } else {
        // Leave a core for the main thread, taskflow already uses the others for chunks and recording
        int threads = config.threads > 1 ? config.threads : std::max(1, (int) std::thread::hardware_concurrency() / 2 - 1);
        job_system = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
    }

    physics = std::make_unique<JPH::PhysicsSystem>();
    physics->Init(config.max_bodies, 0, config.max_body_pairs, config.max_contacts, broadphase_layers, object_vs_broadphase, object_pairs);
    physics->SetGravity(toJolt(config.gravity));

    JPH::PhysicsSettings ps = physics->GetPhysicsSettings();
    if(config.velocity_steps) ps.mNumVelocitySteps = *config.velocity_steps;
    if(config.position_steps) ps.mNumPositionSteps = *config.position_steps;
    if(config.speculative_distance) ps.mSpeculativeContactDistance = *config.speculative_distance;
    if(config.penetration_slop) ps.mPenetrationSlop = *config.penetration_slop;
    physics->SetPhysicsSettings(ps);

    // Jolt keeps a plain function pointer: one function per rule
    if(config.friction == Combine::Max) {
        physics->SetCombineFriction([](const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) { return combine(Combine::Max, a.GetFriction(), b.GetFriction()); });
    } else if(config.friction == Combine::Min) {
        physics->SetCombineFriction([](const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) { return combine(Combine::Min, a.GetFriction(), b.GetFriction()); });
    }
    if(config.restitution == Combine::Max) {
        physics->SetCombineRestitution([](const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) { return combine(Combine::Max, a.GetRestitution(), b.GetRestitution()); });
    } else if(config.restitution == Combine::Min) {
        physics->SetCombineRestitution([](const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) { return combine(Combine::Min, a.GetRestitution(), b.GetRestitution()); });
    }

}

bool PhysicsWorld::singleThreaded() const {
    return single_threaded;
}

void PhysicsWorld::step(float dt) {
    physics->Update(dt, 1, temp_allocator.get(), job_system.get());
}

PhysicsWorld::~PhysicsWorld() {

    physics.reset();
    job_system.reset();
    temp_allocator.reset();

}
