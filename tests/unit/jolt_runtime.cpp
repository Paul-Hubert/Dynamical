#include <doctest/doctest.h>

#include <thread>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include "physics/jolt_runtime.h"
#include "physics/physics_world.h"

using namespace dy;

namespace {

// A world with a few falling boxes, stepped a little, then destroyed
void useWorld() {
    PhysicsWorld world;
    auto& bodies = world.bodies();
    for(int i = 0; i < 4; i++) {
        JPH::BodyCreationSettings settings(new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f)), JPH::RVec3(0.f, 2.f * i, 0.f),
            JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, PhysicsLayers::moving_layer);
        bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
    }
    for(int s = 0; s < 10; s++) world.step(1.f / 60.f);
}

}

TEST_SUITE("unit") {

TEST_CASE("T2.1 JoltRuntime: 100 worlds sequentially and 8 concurrently leave no allocation behind") {

    REQUIRE(JoltRuntime::active());
    const int64_t baseline = JoltRuntime::liveAllocations();

    for(int i = 0; i < 100; i++) useWorld();
    CHECK(JoltRuntime::liveAllocations() == baseline);

    std::vector<std::thread> threads;
    for(int i = 0; i < 8; i++) threads.emplace_back(useWorld);
    for(auto& t : threads) t.join();
    CHECK(JoltRuntime::liveAllocations() == baseline);

}

}

TEST_SUITE("unit") {

TEST_CASE("T2.2 a world with threads=1 uses JobSystemSingleThreaded and base keeps the thread pool") {

    PhysicsConfig single;
    single.threads = 1;
    PhysicsWorld training(single);
    CHECK(training.singleThreaded());

    // base: the default config, master's thread pool
    entt::registry reg;
    CHECK(!physicsWorld(reg).singleThreaded());
    CHECK(&physicsWorld(reg) == reg.ctx().find<PhysicsWorld>());

}

}
