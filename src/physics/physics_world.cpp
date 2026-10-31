#include "physics_world.h"

#include "jolt_runtime.h"

#include "util/log.h"

#include <algorithm>
#include <thread>

using namespace dy;

constexpr JPH::uint max_bodies = 65536;
constexpr JPH::uint max_body_pairs = 65536;
constexpr JPH::uint max_contact_constraints = 16384;

PhysicsWorld::PhysicsWorld() {

    if(!JoltRuntime::active()) dy::log(dy::critical) << "PhysicsWorld created without a JoltRuntime\n";

    temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);

    // Leave a core for the main thread, taskflow already uses the others for chunks and recording
    int threads = std::max(1, (int) std::thread::hardware_concurrency() / 2 - 1);
    job_system = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);

    physics = std::make_unique<JPH::PhysicsSystem>();
    physics->Init(max_bodies, 0, max_body_pairs, max_contact_constraints, broadphase_layers, object_vs_broadphase, object_pairs);
    physics->SetGravity(JPH::Vec3(0.f, -9.81f, 0.f));

}

void PhysicsWorld::step(float dt) {
    physics->Update(dt, 1, temp_allocator.get(), job_system.get());
}

PhysicsWorld::~PhysicsWorld() {

    physics.reset();
    job_system.reset();
    temp_allocator.reset();

}
