#include <doctest/doctest.h>

#include <taskflow/taskflow.hpp>

#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"
#include "support/test_systems.h"

using namespace dy;

namespace {

SceneLoadOptions testResources() {
    SceneLoadOptions options;
    options.resources = "tests/data/scene/resources";
    return options;
}

}

TEST_SUITE("unit") {

TEST_CASE("Worlds: clock and phases and physics and seeds from the template") {

    test::registerTestSystems();
    ScenarioRunner runner("worlds", {}, testResources(), 7);
    REQUIRE(runner.worldCount() == 3);

    World& w = runner.world(1);
    CHECK(w.index == 1);
    CHECK(w.reg.ctx().get<WorldClock>().decision_period == 2);
    CHECK(w.reg.ctx().get<PhysicsWorld>().singleThreaded());
    CHECK(w.reg.ctx().get<WorldInfo>().seed == worldSeed(7, 1));
    CHECK(worldSeed(7, 0) != worldSeed(7, 1));
    CHECK(worldSeed(7, 1) != worldSeed(8, 1));
    CHECK(w.systems.names(SystemSet::List::Fixed) == std::vector<std::string> { "TestWorld", "RigidBody", "PhysicsStep", "TestBoth" });

    // The crate (half extent 0.25) falls on the static box (top at y = 0) and rests
    tf::Executor executor(2);
    runner.step(180, &executor);
    for(size_t i = 0; i < runner.worldCount(); i++) {
        auto& reg = runner.world(i).reg;
        int dynamic = 0;
        for(auto [e, t, b] : reg.view<Transform, RigidBody>().each()) {
            if(b.motion != RigidBody::Motion::Dynamic) continue;
            dynamic++;
            CHECK(t.position.y == doctest::Approx(0.25).epsilon(0.02));
        }
        CHECK(dynamic == 1);
        CHECK(reg.ctx().get<WorldClock>().tick == 180);
    }
    // Same template and no randomness: identical worlds
    CHECK(runner.world(0).hash() == runner.world(2).hash());

}

TEST_CASE("WorldRng: the same seed gives the same stream and the distributions stay in range") {
    WorldRng a(42), b(42), c(43);
    for(int i = 0; i < 100; i++) {
        double x = a.uniform();
        CHECK(x == b.uniform());
        CHECK(x >= 0.);
        CHECK(x < 1.);
    }
    CHECK(a.uniform() != c.uniform());
    glm::vec3 v = a.unitSphere();
    CHECK(glm::length(v) == doctest::Approx(1.f).epsilon(1e-5));
    double sum = 0., sq = 0.;
    for(int i = 0; i < 20000; i++) {
        double n = a.normal();
        sum += n;
        sq += n * n;
    }
    CHECK(sum / 20000. == doctest::Approx(0.).epsilon(0.03));
    CHECK(sq / 20000. == doctest::Approx(1.).epsilon(0.03));
}

}
