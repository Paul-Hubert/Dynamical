#include <doctest/doctest.h>

#include <taskflow/taskflow.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <set>

#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"

using namespace dy;

namespace {

std::vector<uint64_t> hashes(ScenarioRunner& runner) {
    std::vector<uint64_t> out;
    for(size_t i = 0; i < runner.worldCount(); i++) out.push_back(runner.world(i).hash());
    return out;
}

}

TEST_SUITE("scenario") {

TEST_CASE("S2.1 test_boxes: after 600 ticks every box rests on the ground of its world") {

    ScenarioRunner runner("test_boxes", {}, SceneLoadOptions {});
    REQUIRE(runner.worldCount() == 25);
    tf::Executor executor;
    runner.step(600, &executor);

    int bodies = 0, failures = 0;
    float worst_height = 0.f, worst_speed = 0.f;
    for(size_t w = 0; w < runner.worldCount(); w++) {
        auto& reg = runner.world(w).reg;
        auto& jolt = reg.ctx().get<PhysicsWorld>().bodies();
        for(auto [e, t, b] : reg.view<Transform, RigidBody>().each()) {
            REQUIRE(b.created());
            bodies++;
            // A cube rests on a face; a capsule lies on its side
            float expected = b.shape == RigidBody::Shape::Capsule ? b.half_extent.x : b.half_extent.y;
            float height = std::abs(t.position.y - expected);
            float speed = glm::length(toGlm(jolt.GetLinearVelocity(JPH::BodyID(b.body))));
            worst_height = std::max(worst_height, height);
            worst_speed = std::max(worst_speed, speed);
            if(height > 0.002f || speed >= 1e-3f) {
                failures++;
                MESSAGE("world " << w << " entity " << entt::to_integral(e) << (b.shape == RigidBody::Shape::Capsule ? " capsule" : " box")
                    << " y " << t.position.y << " expected " << expected << " speed " << speed);
            }
        }
    }
    MESSAGE(bodies << " bodies, worst height error " << worst_height * 1000.f << " mm, worst speed " << worst_speed << " m/s");
    CHECK(bodies == 25 * 50);
    CHECK(failures == 0);

}

TEST_CASE("S2.2 test_boxes is deterministic with 1 and 8 threads") {

    ScenarioRunner one("test_boxes", {}, SceneLoadOptions {});
    ScenarioRunner eight("test_boxes", {}, SceneLoadOptions {});
    tf::Executor single(1), many(8);
    one.step(600, &single);
    eight.step(600, &many);
    CHECK(hashes(one) == hashes(eight));

}

TEST_CASE("S2.3 world isolation: different seeds differ and the same seed in two worlds gives equal hashes") {

    ScenarioRunner runner("test_boxes", { { "num_worlds", "6" } }, SceneLoadOptions {}, 11);
    runner.step(300);
    auto h = hashes(runner);
    CHECK(std::set<uint64_t>(h.begin(), h.end()).size() == h.size());

    // Two worlds of different index built with the same seed
    SceneDef def = loadScene("test_boxes", { { "num_worlds", "1" } }, SceneLoadOptions {});
    World a(0, 1234, def.worlds[0]), b(17, 1234, def.worlds[0]);
    for(World* w : { &a, &b }) {
        w->preinit();
        w->init();
        for(int t = 0; t < 300; t++) w->tick();
    }
    CHECK(a.hash() == b.hash());

}

}
