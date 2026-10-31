#include <doctest/doctest.h>

#include "scene/run_args.h"

using namespace dy;

TEST_SUITE("unit") {

TEST_CASE("RunArgs parses scene, process, capture and probe keys") {
    auto r = parseRunArgs({ "scene=base", "var.num_worlds=4", "mode=bench", "threads=8", "ticks=600", "episodes=3",
        "capture=tests/capture/base.capture.json", "capture_dir=out", "window_hidden=false", "exit_after_capture=0",
        "probe=p.csv", "probe_entities=RigidBody", "probe_every=30" });
    CHECK(r.errors.empty());
    CHECK(r.unknown.empty());
    CHECK(r.args.scene == "base");
    REQUIRE(r.args.vars.size() == 1);
    CHECK(r.args.vars[0].first == "num_worlds");
    CHECK(r.args.vars[0].second == "4");
    CHECK(r.args.mode == "bench");
    CHECK(r.args.threads == 8);
    CHECK(r.args.ticks == 600);
    CHECK(r.args.episodes == 3);
    CHECK(r.args.capture == "tests/capture/base.capture.json");
    CHECK(r.args.capture_dir == "out");
    CHECK(r.args.window_hidden == false);
    CHECK(r.args.exit_after_capture == false);
    CHECK(r.args.probe == "p.csv");
    CHECK(r.args.probe_every == 30);
}

TEST_CASE("RunArgs keeps variables in command line order") {
    auto r = parseRunArgs({ "var.x=1", "var.y=a", "var.x=2" });
    REQUIRE(r.args.vars.size() == 3);
    CHECK(r.args.vars[2].first == "x");
    CHECK(r.args.vars[2].second == "2");
}

TEST_CASE("RunArgs fixed_frame_dt accepts fractions and decimals") {
    CHECK(parseRunArgs({ "fixed_frame_dt=1/60" }).args.fixed_frame_dt == doctest::Approx(1. / 60.).epsilon(1e-15));
    CHECK(parseRunArgs({ "fixed_frame_dt=0.02" }).args.fixed_frame_dt == 0.02);
    CHECK(!parseRunArgs({ "fixed_frame_dt=1/0" }).errors.empty());
    CHECK(!parseRunArgs({ "fixed_frame_dt=fast" }).errors.empty());
}

TEST_CASE("RunArgs capture and probe default to a fixed clock and a hidden window") {
    auto plain = parseRunArgs({ "scene=base" });
    CHECK(!plain.args.deterministic());
    CHECK(!plain.args.window_hidden);

    auto capture = parseRunArgs({ "capture=a.json" });
    CHECK(capture.args.fixed_frame_dt == 1. / 60.);
    CHECK(capture.args.window_hidden);
    CHECK(capture.args.deterministic());

    auto probe = parseRunArgs({ "probe=a.csv", "fixed_frame_dt=0.01", "window_hidden=false" });
    CHECK(probe.args.fixed_frame_dt == 0.01);
    CHECK(!probe.args.window_hidden);
}

TEST_CASE("RunArgs reports unknown keys and bad values") {
    auto r = parseRunArgs({ "window_width=960", "threads=-2", "ticks=abc", "probe_every=0", "window_hidden=yes" });
    REQUIRE(r.unknown.size() == 1);
    CHECK(r.unknown[0] == "window_width");
    CHECK(r.errors.size() == 4);
}

}
