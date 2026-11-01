#include <doctest/doctest.h>

#include <filesystem>

#include "scene/scene_def.h"
#include "support/test_systems.h"

using namespace dy;

namespace {

SceneLoadOptions testResources(bool allow_renderer = true) {
    SceneLoadOptions options;
    options.resources = "tests/data/scene/resources";
    options.allow_renderer = allow_renderer;
    return options;
}

struct Fixture {
    const char* scene;
    const char* file;
    const char* path;
    const char* message;
    bool allow_renderer = true;
};

}

TEST_SUITE("unit") {

TEST_CASE("T1.3 malformed scenes fail with the file and JSON path") {

    test::registerTestSystems();

    const Fixture fixtures[] = {
        { "bad/unknown_system", "bad/unknown_system.scene.json", "/host/pre/1", "unknown system Nope" },
        { "bad/bad_type", "bad/bad_type.scene.json", "/renderer/vr", "expected true or false" },
        { "bad/missing_variable", "bad/missing_variable.scene.json", "/host/pre/0/params/x", "unknown variable ${nope}" },
        { "bad/cyclic_a", "bad/cyclic_b.json", "/include/0", "cyclic include" },
        { "bad/renderer_in_train", "bad/renderer_in_train.scene.json", "/renderer", "renderer block is not allowed", false },
        { "bad/unknown_key", "bad/unknown_key.scene.json", "/hosts", "unknown key" },
        { "bad/unknown_component", "bad/unknown_component.scene.json", "/templates/t/entities/0/Nope", "unknown component Nope" },
        { "bad/unknown_prefab", "bad/unknown_prefab.scene.json", "/templates/t/entities/0/prefab", "unknown prefab nope" },
        { "bad/world_system_at_host", "bad/world_system_at_host.scene.json", "/host/pre/0", "world system, used at host scope" },
        { "bad/host_system_in_world", "bad/host_system_in_world.scene.json", "/templates/t/fixed/0", "host system, used in a world" },
        { "bad/rendered_without_desktop", "bad/rendered_without_desktop.scene.json", "/renderer", "must include common/desktop.json" },
        { "bad/bad_variable_type", "bad/bad_variable_type.scene.json", "/variables/n/type", "unknown type 'vec3'" },
        { "bad/bad_default", "bad/bad_default.scene.json", "/variables/n/default", "is not a int" },
        { "bad/missing_include", "bad/missing_include.scene.json", "/include/0", "include not found" },
        { "bad/bad_component_field", "bad/bad_component_field.scene.json", "/templates/t/entities/0/RigidBody/half_extent", "array of 3 numbers" },
        { "bad/world_unknown_variable", "bad/world_unknown_variable.scene.json", "/worlds/0/variables/sise", "unknown variable sise" },
        { "bad/world_variable_type", "bad/world_variable_type.scene.json", "/worlds/0/variables/size", "\"big\" is not a float" },
    };

    int failed_as_expected = 0;
    for(auto& f : fixtures) {
        CAPTURE(f.scene);
        try {
            loadScene(f.scene, {}, testResources(f.allow_renderer));
            FAIL("loaded without error");
        } catch(const SceneError& e) {
            CAPTURE(e.what());
            CHECK(e.file == f.file);
            CHECK(e.path == f.path);
            CHECK(std::string(e.what()).find(f.message) != std::string::npos);
            if(e.file == f.file && e.path == f.path) failed_as_expected++;
        }
    }
    CHECK(failed_as_expected == 17);

}

TEST_CASE("T1.4 variables: defaults and command line overrides (last wins) with typed substitution and file references") {

    test::registerTestSystems();

    SceneDef def = loadScene("vars", {}, testResources());
    REQUIRE(def.host_pre.size() == 4);
    CHECK(def.host_pre[0].name == "TestHost2");    // from the include, which comes first
    const auto& p = def.host_pre[2].params;
    CHECK(def.host_pre[2].name == "TestHost");
    CHECK(p["n"].is_number_integer());
    CHECK(p["n"] == 4);
    CHECK(p["speed"].is_number_float());
    CHECK(p["speed"] == 1.5);
    CHECK(p["on"] == true);
    CHECK(p["label"] == "run_abc_4");               // the scene's default replaces the fragment's
    CHECK(p["rate"] == 60);
    CHECK(p["nested"] == 30);
    REQUIRE(def.worlds.size() == 1);
    CHECK(def.worlds[0].count == 4);

    SceneDef over = loadScene("vars", { { "n", "7" }, { "label", "x" }, { "n", "9" }, { "speed", "2" }, { "on", "false" } }, testResources());
    const auto& q = over.host_pre[2].params;
    CHECK(q["n"] == 9);
    CHECK(q["label"] == "run_x_9");
    CHECK(q["speed"].is_number_float());
    CHECK(q["speed"] == 2.0);
    CHECK(q["on"] == false);
    CHECK(over.worlds[0].count == 9);

    CHECK_THROWS_AS(loadScene("vars", { { "n", "abc" } }, testResources()), SceneError);
    try {
        loadScene("vars", { { "zzz", "1" } }, testResources());
        FAIL("unknown variable accepted");
    } catch(const SceneError& e) {
        CHECK(e.file == "<command line>");
        CHECK(e.path == "var.zzz");
    }

}

TEST_CASE("T1.5 world entries with variables of their own resolve their template again and survive the resolved dump") {

    test::registerTestSystems();

    auto check = [](const SceneDef& def, double size0, double size1, double size2) {
        REQUIRE(def.worlds.size() == 3);
        const double sizes[3] = { size0, size1, size2 };
        const char* combines[3] = { "geometric", "geometric", "min" };
        for(int i = 0; i < 3; i++) {
            CAPTURE(i);
            CHECK(def.worlds[i].template_name == "drop");
            CHECK(def.worlds[i].entities[0].components["RigidBody"]["half_extent"] == nlohmann::json { sizes[i], sizes[i], sizes[i] });
            CHECK(def.worlds[i].physics["friction_combine"] == combines[i]);
        }
    };

    // Entry 1: its own value; entry 2: the file's defaults, then its own value over them
    SceneDef def = loadScene("world_vars", {}, testResources());
    check(def, 0.5, 2.0, 4.0);
    CHECK(def.worlds[0].count == 2);
    CHECK(def.worlds[1].count == 1);
    CHECK(def.resolved["variables"]["size"]["default"] == 0.5);
    CHECK(def.resolved["templates"].contains("drop@1"));
    CHECK(def.resolved["templates"].contains("drop@2"));
    CHECK(!def.resolved["worlds"][1].contains("variables"));

    // The command line sets the scene's variables; a world's own win over it
    check(loadScene("world_vars", { { "size", "1" } }, testResources()), 1.0, 2.0, 4.0);

    auto file = std::filesystem::temp_directory_path() / "dynamical_tests_world_vars.resolved.json";
    writeResolved(def, file);
    SceneDef again = loadScene(file.string(), {}, testResources());
    check(again, 0.5, 2.0, 4.0);
    CHECK(again.resolved == def.resolved);
    std::filesystem::remove(file);

}

TEST_CASE("T1.4 resolved dump round trip") {

    test::registerTestSystems();

    SceneDef def = loadScene("vars", { { "n", "5" } }, testResources());
    auto file = std::filesystem::temp_directory_path() / "dynamical_tests_vars.resolved.json";
    writeResolved(def, file);

    SceneDef again = loadScene(file.string(), {}, testResources());
    CHECK(again.resolved == def.resolved);
    CHECK(again.worlds[0].count == 5);
    CHECK(again.renderer.has_value());
    std::filesystem::remove(file);

}

TEST_CASE("Scene loader: renderer and host order and world systems and prefabs") {

    test::registerTestSystems();

    SceneDef def = loadScene("vars", {}, testResources());

    REQUIRE(def.renderer);
    CHECK(def.renderer->passes == std::vector<std::string> { "terrain", "model", "ui" });
    CHECK(def.renderer->vr == false);

    // desktop.json lists TestInput and TestUI; the scene lists them again around TestHost: the scene's order wins
    std::vector<std::string> pre;
    for(auto& e : def.host_pre) pre.push_back(e.name);
    CHECK(pre == std::vector<std::string> { "TestHost2", "TestInput", "TestHost", "TestUI" });
    REQUIRE(def.host_post.size() == 1);

    const WorldDef& w = def.worlds[0];
    CHECK(w.physics["threads"] == 1);
    REQUIRE(w.fixed.size() == 3);
    CHECK(w.fixed[0].name == "TestBoth");
    CHECK(w.fixed[0].phase == Phase::Act);
    CHECK(w.fixed[1].phase == Phase::Every);
    CHECK(w.fixed[2].name == "TestWorld");
    CHECK(w.fixed[2].phase == Phase::Observe);

    REQUIRE(w.entities.size() == 1);
    const auto& c = w.entities[0].components;
    CHECK(c["RigidBody"]["shape"] == "box");                 // base_box
    CHECK(c["RigidBody"]["half_extent"][0] == 0.25);         // crate extends base_box
    CHECK(c["RigidBody"]["friction"] == 0.25);               // scene override
    CHECK(c["ModelRef"]["path"] == "models/box.gltf");
    CHECK(c.contains("Transform"));

}

TEST_CASE("Scene loader: more refusals") {

    test::registerTestSystems();

    auto expectError = [](const char* scene, const char* path, bool allow_renderer = true) {
        try {
            loadScene(scene, {}, testResources(allow_renderer));
            FAIL("loaded without error");
        } catch(const SceneError& e) {
            CAPTURE(e.what());
            CHECK(e.path == path);
        }
    };

    // The vars scene renders: refused by dynamical_train
    expectError("vars", "/renderer", false);
    expectError("missing_scene", "");

}

}
