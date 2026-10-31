#include <doctest/doctest.h>

#include "logic/systems/system_set.h"
#include "scene/scene_def.h"
#include "scene/system_registry.h"
#include "support/test_systems.h"

using namespace dy;

TEST_SUITE("unit") {

TEST_CASE("T1.5 base resolves to master's system order as registry names") {

    test::registerViewSystemStubs();

    SceneDef def = loadScene("base", {}, SceneLoadOptions {});

    entt::registry reg;
    SystemSet set(reg);
    for(auto& e : def.host_pre) set.add(SystemSet::List::Pre, findSystem(e.name)->make(reg, e.params), e.phase, e.name);
    for(auto& e : def.host_post) set.add(SystemSet::List::Post, findSystem(e.name)->make(reg, e.params), e.phase, e.name);

    // master's Game::start, with SceneSys replaced by BoxRing, ModelLineup, BoxThrower where it was
    CHECK(set.names(SystemSet::List::Pre) == std::vector<std::string> {
        "Input", "UI", "Time", "PlayerControl", "VRControl", "DebugMenu",
        "ChunkManager", "ChunkGenerator", "BoxRing", "ModelLineup", "BoxThrower", "Physics" });
    CHECK(set.names(SystemSet::List::Post) == std::vector<std::string> { "Terrain" });

    REQUIRE(def.renderer);
    CHECK(def.renderer->passes == std::vector<std::string> { "terrain", "grass", "model", "ui" });
    CHECK(def.renderer->vr);
    CHECK(def.worlds.empty());

    // dynamical_train refuses it
    SceneLoadOptions train;
    train.allow_renderer = false;
    CHECK_THROWS_AS(loadScene("base", {}, train), SceneError);

}

}
