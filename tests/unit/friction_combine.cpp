#include <doctest/doctest.h>

#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "scene/scenario_runner.h"

using namespace dy;

namespace {

// Distance the box (mu 1) travels in 2 s on a 30 degree incline (mu 0.2)
float slide(const char* combine) {
    SceneLoadOptions options;
    options.resources = "tests/data/scene/resources";
    ScenarioRunner runner("incline", { { "combine", combine } }, options);
    auto& reg = runner.world(0).reg;
    runner.step(1);
    glm::vec3 start(0.f);
    for(auto [e, t, b] : reg.view<Transform, RigidBody>().each()) {
        if(b.motion == RigidBody::Motion::Dynamic) start = t.position;
    }
    runner.step(120);
    for(auto [e, t, b] : reg.view<Transform, RigidBody>().each()) {
        if(b.motion == RigidBody::Motion::Dynamic) return glm::length(t.position - start);
    }
    return -1.f;
}

}

TEST_SUITE("scenario") {

TEST_CASE("S2.4 friction combine: mu 1 max-combined with 0.2 holds on a 30 degree incline and min-combined slides") {
    float held = slide("max");
    float slid = slide("min");
    MESSAGE("max combine: moved " << held * 1000.f << " mm; min combine: slid " << slid << " m");
    CHECK(held < 0.001f);
    CHECK(slid > 0.5f);
}

}
