#include "physics_world.h"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>

#include "util/log.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <thread>

using namespace dy;

constexpr JPH::uint max_bodies = 65536;
constexpr JPH::uint max_body_pairs = 65536;
constexpr JPH::uint max_contact_constraints = 16384;

static void joltTrace(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    dy::log(dy::info) << "[Jolt] " << buffer << "\n";
}

#ifdef JPH_ENABLE_ASSERTS
static bool joltAssert(const char* expression, const char* message, const char* file, JPH::uint line) {
    dy::log(dy::error) << "[Jolt] assert " << expression << " " << (message ? message : "") << " at " << file << ":" << line << "\n";
    return true; // break into the debugger
}
#endif

PhysicsWorld::PhysicsWorld() {

    JPH::RegisterDefaultAllocator();
    JPH::Trace = joltTrace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = joltAssert;)

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

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

    // Bodies and shapes must be gone before the types are unregistered
    physics.reset();
    job_system.reset();
    temp_allocator.reset();

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

}
