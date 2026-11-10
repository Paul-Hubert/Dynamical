#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "logic/agents/agent_io.h"
#include "logic/agents/gait.h"
#include "logic/agents/ground_field.h"
#include "logic/agents/humanbot.h"
#include "logic/agents/walk_routes.h"
#include "logic/terrain/marching_cubes.h"
#include "logic/terrain/terrain_generator.h"
#include "physics/physics_world.h"
#include "physics/terrain_colliders.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"

using namespace dy;

// The walker on the marching cubes terrain (docs/physical-agents/terrain-walker.md): routes in a slope band, heights
// measured from the ground, the kinematic walk as demonstrations

namespace {

const float deg = 180.f / 3.14159265358979f;

bool haveAssets() {
    return std::filesystem::exists("resources/agents/bodies/heidelbot_notoes.body.json") && std::filesystem::exists("resources/mclookup.bin");
}

using Vars = std::vector<std::pair<std::string, std::string>>;

// The lowest sole corner of the ragdoll over the ground under it, m
float soleClearance(const HumanBot& bot) {
    RigModel model(bot.body);
    float lowest = 1e9f;
    for(int side = 0; side < 2; side++) {
        FootGeometry sole = footGeometry(model, side == 1);
        for(int c = 0; c < 4; c++) {
            glm::vec3 corner = sole.corner(c, bot.state.position[sole.segment], bot.state.rotation[sole.segment]);
            lowest = std::min(lowest, corner.y - bot.ground->height(corner.x, corner.z, corner.y + 2.f, corner.y - 100.f));
        }
    }
    return lowest;
}

uint64_t stateHash(const HumanBot& bot) {
    uint64_t h = 1469598103934665603ull;
    for(size_t i = 0; i < bot.bodies.size(); i++) {
        for(int k = 0; k < 3; k++) {
            uint32_t bits;
            std::memcpy(&bits, &bot.state.position[i][k], 4);
            h = (h ^ bits) * 1099511628211ull;
        }
    }
    return h;
}

}

TEST_SUITE("unit") {

TEST_CASE("TW.1 a route's slopes are the plane's along and across its direction") {
    struct Case { float pitch, roll, heading; };
    for(Case c : { Case { 0.f, 0.f, 30.f }, Case { 12.f, 0.f, 0.f }, Case { 12.f, 0.f, 180.f }, Case { 12.f, 0.f, 90.f }, Case { -8.f, 10.f, 0.f }, Case { 15.f, -6.f, 45.f } }) {
        PlaneField plane(c.pitch, c.roll);
        float h = c.heading / deg;
        glm::vec3 dir(-std::sin(h), 0.f, -std::cos(h)), right(std::cos(h), 0.f, -std::sin(h));
        glm::vec3 n = plane.normal();
        float along = std::atan2(-glm::dot(n, dir), n.y), across = std::atan2(-glm::dot(n, right), n.y);
        glm::vec3 from = plane.top(3.f, -2.f)->point;
        RouteSlope m = measureRoute(plane, from, dir, 10.f, 1.f);
        CAPTURE(c.pitch);
        CAPTURE(c.roll);
        CAPTURE(c.heading);
        CHECK(m.ground);
        CHECK(m.up == doctest::Approx(std::max(along, 0.f)).epsilon(0.01).scale(1.f));
        CHECK(m.down == doctest::Approx(std::max(-along, 0.f)).epsilon(0.01).scale(1.f));
        CHECK(m.across == doctest::Approx(std::abs(across)).epsilon(0.01).scale(1.f));
        CHECK(m.mean_across == doctest::Approx(across).epsilon(0.01).scale(1.f));
        CHECK(m.rise == doctest::Approx(10.f * std::tan(along)).epsilon(0.01).scale(1.f));
    }
}

TEST_CASE("TW.2 directions are drawn in the band: near the contour lines of a plane steeper than the band") {
    PlaneField plane(12.f, 0.f);
    WalkRoutes routes;
    routes.up = routes.down = 6.f / deg;
    routes.across = 15.f / deg;
    WorldRng rng(3);
    int found = 0;
    for(int n = 0; n < 200; n++) {
        auto dir = drawDirection(plane, rng, routes, glm::vec3(0.f));
        if(!dir) continue;
        found++;
        // The plane rises toward -z: a path climbing no more than 6 degrees points within 30 degrees of +-x
        CHECK(std::abs(dir->z) < std::tan(6.f / deg) / std::tan(12.f / deg) + 1e-3f);
        CHECK(glm::length(*dir) == doctest::Approx(1.f));
    }
    CHECK(found > 190);

    // No direction at all across a slope steeper than the band
    routes.across = 6.f / deg;
    CHECK(!drawDirection(plane, rng, routes, glm::vec3(0.f)).has_value());
    RouteStart start = drawStart(plane, rng, routes);
    CHECK(!start.found);
    CHECK(start.tries == routes.spawn_tries);
}

TEST_CASE("TW.3 spawn points on the terrain: level enough around them and a path in the band") {
    if(!haveAssets()) return;
    TerrainField field(std::make_shared<TerrainGenerator>(1337), std::make_shared<MarchingCubes>());
    WalkRoutes routes;
    WorldRng rng(11);
    int tries = 0;
    float steepest_up = 0.f, steepest_down = 0.f, steepest_across = 0.f;
    int up = 0, down = 0, level = 0;
    for(int n = 0; n < 60; n++) {
        RouteStart start = drawStart(field, rng, routes);
        REQUIRE(start.found);
        tries += start.tries;
        CHECK(glm::length(glm::vec2(start.point.x, start.point.z)) <= routes.spawn_radius + 1e-3f);
        CHECK(std::abs(field.top(start.point.x, start.point.z)->point.y - start.point.y) < 1e-4f);
        RouteSlope m = measureRoute(field, start.point, start.direction, routes.length, routes.half_width);
        CHECK(inBand(m, routes));
        steepest_up = std::max(steepest_up, m.up);
        steepest_down = std::max(steepest_down, m.down);
        steepest_across = std::max(steepest_across, m.across);
        float pitch = std::atan2(m.rise, routes.length) * deg;
        (pitch > 4.f ? up : pitch < -4.f ? down : level)++;
    }
    CHECK(steepest_up <= routes.up);
    CHECK(steepest_down <= routes.down);
    CHECK(steepest_across <= routes.across);
    MESSAGE("60 spawn points in ", tries, " draws; routes up ", up, ", level ", level, ", down ", down, "; steepest up ", steepest_up * deg, ", down ", steepest_down * deg,
        ", across ", steepest_across * deg, " deg; ", field.blocks(), " blocks");
    CHECK(tries < 60 * 300);
    CHECK(up > 5);
    CHECK(level > 5);
    CHECK(field.blocks() <= TerrainField::max_blocks);
}

TEST_CASE("TW.4 the head height is the same with a level ground field and without one") {
    if(!haveAssets()) return;
    ScenarioRunner runner("walker_train", { { "num_worlds", "1" } }, SceneLoadOptions {});
    auto& world = runner.world(0);
    auto& bot = world.reg.ctx().get<HumanBot>();
    CHECK(!bot.ground);
    CHECK(!bot.routes.has_value());
    for(int t = 0; t < 40; t++) {
        world.tick();
        float plain = bot.currentHeight();
        bot.ground = std::make_shared<PlaneField>();
        float level = bot.currentHeight();
        bot.ground.reset();
        uint32_t a, b;
        std::memcpy(&a, &plain, 4);
        std::memcpy(&b, &level, 4);
        CHECK(a == b);
    }
}

}

TEST_SUITE("scenario") {

TEST_CASE("TW.5 a ragdoll spawns on the terrain as on flat ground: on its soles and at its height with its target on a path in the band") {
    if(!haveAssets()) return;
    ScenarioRunner flat("walker_train", { { "num_worlds", "1" } }, SceneLoadOptions {}, 5);
    ScenarioRunner terrain("walker_terrain_train", { { "num_worlds", "6" } }, SceneLoadOptions {}, 5);
    const auto& reference = flat.world(0).reg.ctx().get<HumanBot>();
    const float height0 = reference.currentHeight();
    CHECK(height0 == doctest::Approx(reference.est0).epsilon(1e-4));

    for(size_t w = 0; w < terrain.worldCount(); w++) {
        auto& reg = terrain.world(w).reg;
        auto& bot = reg.ctx().get<HumanBot>();
        REQUIRE(bot.ground);
        REQUIRE(bot.routes.has_value());
        CHECK(reg.ctx().contains<TerrainUsers>());
        const BodyDef& def = *bot.body;

        // Where the ground is: the origin on it, the soles on it or just above their lowest corner
        CHECK(std::abs(bot.ground->top(bot.origin.x, bot.origin.z)->point.y - bot.origin.y) < 1e-3f);
        CHECK(glm::length(horizontal(bot.origin)) <= bot.routes->spawn_radius + 1e-3f);
        float clearance = soleClearance(bot);
        CHECK(clearance > 0.f);
        CHECK(clearance < 0.004f);

        // The fall test and the observation's head error see a standing walker, whatever the slope under it
        float height = bot.currentHeight();
        CHECK(height > 0.93f * bot.est0);
        CHECK(height < 1.07f * bot.est0);
        CHECK(!bot.isDying());

        // The target: far along a path that stays in the band
        glm::vec3 spine = bot.state.position[def.spine];
        glm::vec3 toward = safeNormalize(horizontal(bot.target - spine));
        RouteSlope m = measureRoute(*bot.ground, bot.origin, toward, bot.routes->length, bot.routes->half_width);
        CHECK(inBand(m, *bot.routes));
        CHECK(glm::length(horizontal(bot.target - spine)) > 500.f);
        MESSAGE("world ", w, ": spawn ", bot.origin.x, " ", bot.origin.y, " ", bot.origin.z, " after ", bot.spawn_tries, " draws, soles ", clearance * 1e3f, " mm over the ground, height ",
            height, " (flat ", height0, "), path up ", m.up * deg, " down ", m.down * deg, " across ", m.across * deg, " deg");
    }
}

TEST_CASE("TW.6 a limp ragdoll on the terrain lands on the colliders and is killed as on flat ground and the next episode is elsewhere") {
    if(!haveAssets()) return;
    // Zero actions: mid targets at half gains, the ragdoll sags and falls
    auto run = [](const char* scene, uint64_t seed, int& killed_at, float& lowest, glm::vec3& first, glm::vec3& second) {
        ScenarioRunner runner(scene, { { "num_worlds", "1" } }, SceneLoadOptions {}, seed);
        auto& world = runner.world(0);
        auto& bot = world.reg.ctx().get<HumanBot>();
        first = bot.spawn_spine;
        killed_at = -1;
        lowest = 1e9f;
        for(int t = 0; t < 600 && killed_at < 0; t++) {
            world.tick();
            for(size_t i = 0; i < bot.bodies.size(); i++) {
                glm::vec3 p = bot.state.position[i];
                float ground = bot.ground ? bot.ground->height(p.x, p.z, p.y + 3.f, p.y - 100.f) : 0.f;
                lowest = std::min(lowest, p.y - ground);
            }
            if(bot.killed) killed_at = bot.tick;
        }
        for(int t = 0; t < 4; t++) world.tick();
        second = bot.spawn_spine;
    };
    int flat_tick = 0, terrain_tick = 0;
    float flat_lowest = 0.f, terrain_lowest = 0.f;
    glm::vec3 a, b, c, d;
    run("walker_train", 9, flat_tick, flat_lowest, a, b);
    CHECK(flat_tick > 0);
    for(uint64_t seed : { 9, 10, 11, 12 }) {
        run("walker_terrain_train", seed, terrain_tick, terrain_lowest, c, d);
        MESSAGE("seed ", seed, ": killed at tick ", terrain_tick, " (flat ", flat_tick, "), lowest segment origin ", terrain_lowest, " m over the ground (flat ", flat_lowest, ")");
        CHECK(terrain_tick > 0);
        CHECK(std::abs(terrain_tick - flat_tick) <= std::max(12, flat_tick / 2));
        // Nothing went through the ground: segment origins get as low as on the flat ground (a hand's is 16 mm under it there)
        CHECK(terrain_lowest > flat_lowest - 0.05f);
        // The next episode starts somewhere else, on its ground
        CHECK(glm::length(horizontal(d - c)) > 1.f);
    }
}

TEST_CASE("TW.7 the kinematic walker's height on slopes is its height on level ground: the fall test and the head error do not see the slope") {
    if(!haveAssets()) return;
    float level_low = 0.f, level_high = 0.f;
    struct Case { const char* pitch; const char* roll; const char* heading; };
    for(Case c : { Case { "0", "0", "0" }, Case { "20", "0", "0" }, Case { "-20", "0", "0" }, Case { "0", "15", "0" }, Case { "14", "-10", "300" } }) {
        ScenarioRunner runner("ik_check", { { "pitch", c.pitch }, { "roll", c.roll }, { "heading", c.heading } }, SceneLoadOptions {});
        auto& world = runner.world(0);
        world.tick();
        entt::entity e = *world.reg.view<GaitParams>().begin();
        world.reg.get<GaitParams>(e).slope_slowdown = 0.f;
        world.reg.get<GaitParams>(e).step_rise = 0.f;
        auto& bot = world.reg.ctx().get<HumanBot>();
        REQUIRE(bot.ground);
        float low = 1e9f, high = 0.f, feet = 0.f;
        for(int t = 0; t < 600; t++) {
            world.tick();
            if(t < 240) continue;
            float height = bot.currentHeight();
            low = std::min(low, height);
            high = std::max(high, height);
            feet = std::max(feet, std::abs(bot.state.position[bot.body->left_foot].y - bot.state.position[bot.body->right_foot].y));
            CHECK(!bot.isDying());
        }
        if(c.pitch[0] == '0' && c.roll[0] == '0') { level_low = low; level_high = high; }
        MESSAGE("pitch ", std::string(c.pitch), " roll ", std::string(c.roll), ": height ", low, " to ", high, " (est0 ", bot.est0, "), feet up to ", feet, " m apart in height");
        // Within the crouch a slope asks for (19 cm at -20 degrees), not the 0.3 m between the feet
        CHECK(low > level_low - 0.24f);
        CHECK(high < level_high + 0.06f);
    }
}

namespace {

struct DemoStats {
    int episodes = 0, decisions = 0, strides = 0;
    int warmup_decisions = 0;
    bool flags = true, header = true, stack = true, first_empty = true;
    float slowest = 1e9f, fastest = 0.f;        // m/s over one stride, as the reward measures speed
    float axis_step = 0.f, velocity_step = 0.f, anchor_speed = 0.f, head_error = 0.f;
    float up = 0.f, down = 0.f, across = 0.f;   // the steepest ground met while recording, radians
    int climbing = 0, descending = 0, level = 0;        // episodes by the mean slope of their route
    float crouch = 0.f;
    float nearest_frame = 1.f;                  // the least horizontal length of a frame segment's forward axis: under 0.1 the layout's heading frame falls back, at 0 it flips
    GaitReport report;
};

// Steps a gait demo scene as mode=record-demo does, for `episodes` recorded episodes
DemoStats recordGait(const char* scene, Vars vars, int episodes, int episode_decisions) {
    vars.emplace_back("episode_decisions", std::to_string(episode_decisions));
    ScenarioRunner runner(scene, vars, SceneLoadOptions {}, 4);
    auto& world = runner.world(0);
    auto& io = world.reg.ctx().get<AgentIO>();
    auto& bot = world.reg.ctx().get<HumanBot>();
    const auto& clock = world.reg.ctx().get<WorldClock>();
    const int period = (int) clock.decision_period;
    entt::entity e = entt::null;

    DemoStats s;
    std::vector<float> previous;
    bool recording = false;
    int in_episode = 0;
    bool have_strike = false;
    glm::vec3 strike_spine(0.f), first_spine(0.f);
    double strike_time = 0.;
    float first_ground = 0.f;

    while(s.episodes < episodes) {
        for(int k = 0; k < period; k++) {
            world.tick();
            if(e == entt::null) e = *world.reg.view<GaitState>().begin();
            const auto& state = world.reg.get<GaitState>(e);
            // Recorded from the first tick of the first recorded window; clock.tick is already the next tick's
            const auto& route = world.reg.get<GaitRoute>(e);
            const bool now = clock.tick > route.start_tick + route.warm_ticks;
            if(now && !recording) have_strike = false;
            recording = now;
            if(!route.found) s.flags = false;
            if(!recording) continue;
            for(int j = 0; j < humanbot_joints; j++) {
                float flat = glm::length(horizontal(bot.state.rotation[bot.body->frameSegment(j)] * glm::vec3(0.f, 0.f, -1.f)));
                s.nearest_frame = std::min(s.nearest_frame, flat);
            }
            s.up = std::max(s.up, state.slope_pitch);
            s.down = std::max(s.down, -state.slope_pitch);
            s.across = std::max(s.across, std::abs(state.slope_roll));
            s.crouch = std::max(s.crouch, world.reg.get<BalanceState>(e).crouch);
            if(state.speed != 1.36f) s.flags = false;
            if(state.foot[0].landed) {
                glm::vec3 spine = bot.state.position[bot.body->spine];
                double now = clock.tick * clock.dt;
                if(have_strike) {
                    float speed = glm::dot(horizontal(spine - strike_spine), state.direction()) / (float) (now - strike_time);
                    s.slowest = std::min(s.slowest, speed);
                    s.fastest = std::max(s.fastest, speed);
                    s.strides++;
                }
                have_strike = true;
                strike_spine = spine;
                strike_time = now;
            }
        }
        if(!io.recordable) {
            if(io.done) s.flags = false;
            s.warmup_decisions++;
            in_episode = 0;
            continue;
        }
        const float* frame = io.observation.data() + humanbot_frame;
        if(in_episode == 0) {
            first_spine = bot.state.position[bot.body->spine];
            first_ground = bot.groundUnder(first_spine);
            for(int i = 0; i < humanbot_frame; i++) if(io.observation[i] != 0.f) s.first_empty = false;
        } else {
            for(int i = 0; i < humanbot_frame; i++) if(io.observation[i] != previous[i]) s.stack = false;
            for(int j = 0; j < humanbot_joints; j++) {
                const float* o = frame + 3 + 9 * j;
                const float* p = previous.data() + 3 + 9 * j;
                for(int a = 0; a < 2; a++) s.axis_step = std::max(s.axis_step, glm::length(glm::vec3(o[3 * a], o[3 * a + 1], o[3 * a + 2]) - glm::vec3(p[3 * a], p[3 * a + 1], p[3 * a + 2])));
                s.velocity_step = std::max(s.velocity_step, glm::length(glm::vec3(o[6], o[7], o[8]) - glm::vec3(p[6], p[7], p[8])));
            }
        }
        if(frame[0] != 0.f || frame[2] != 0.f) s.header = false;
        s.head_error = std::max(s.head_error, frame[1]);
        for(int j = 0; j < humanbot_joints; j++) {
            const float* o = frame + 3 + 9 * j + 6;
            s.anchor_speed = std::max(s.anchor_speed, glm::length(glm::vec3(o[0], o[1], o[2])));
        }
        for(float v : io.observation) if(!std::isfinite(v)) s.flags = false;
        previous.assign(frame, frame + humanbot_frame);
        in_episode++;
        s.decisions++;
        if(io.done) {
            if(in_episode != episode_decisions) s.flags = false;
            glm::vec3 spine = bot.state.position[bot.body->spine];
            float run = glm::length(horizontal(spine - first_spine));
            float pitch = std::atan2(bot.groundUnder(spine) - first_ground, run) * deg;
            (pitch > 4.f ? s.climbing : pitch < -4.f ? s.descending : s.level)++;
            s.episodes++;
        } else if(in_episode >= episode_decisions) {
            s.flags = false;
        }
    }
    s.report = world.reg.get<GaitReport>(e);
    return s;
}

std::string describe(const DemoStats& s) {
    const GaitReport& r = s.report;
    char text[900];
    std::snprintf(text, sizeof(text),
        "%d episodes (%d up, %d level, %d down), %d decisions, %d strides at %.4f to %.4f m/s | ground up %.1f down %.1f across %.1f deg, crouch %.0f mm | "
        "over body limits %.2f deg (joint %d), stance ik %.2f mm %.2f deg, swing ik %.1f mm %.2f deg, penetration %.2f mm, slip %.2f mm, com off %.2f mm, outside the prints %.1f mm | "
        "per decision: axes turn up to %.3f, anchor velocities change up to %.2f m/s, anchor speed up to %.2f m/s, head error up to %.3f; frame segments at least %.1f deg from vertical",
        s.episodes, s.climbing, s.level, s.descending, s.decisions, s.strides, s.slowest, s.fastest, s.up * deg, s.down * deg, s.across * deg, s.crouch * 1e3f,
        r.max_body_limit_excess * deg, r.worst_joint, r.max_ik_position * 1e3f, r.max_ik_rotation * deg, r.max_swing_ik_position * 1e3f, r.max_swing_ik_rotation * deg,
        r.max_penetration * 1e3f, r.max_slip * 1e3f, r.max_com_error * 1e3f, r.max_outside * 1e3f, s.axis_step, s.velocity_step, s.anchor_speed, s.head_error, std::asin(std::min(s.nearest_frame, 1.f)) * deg);
    return text;
}

void checkDemo(const DemoStats& s) {
    // The recording conventions of the track demo: no reward, the speed difference zeroed, episodes that start on an empty stack
    CHECK(s.flags);
    CHECK(s.header);
    CHECK(s.stack);
    CHECK(s.first_empty);
    // 1.36 m/s over every stride, as the reward measures it: the spine's horizontal progress along the commanded direction
    CHECK(s.strides >= 3 * s.episodes);
    // The observation's heading frames stay on their main formula: no frame segment's forward axis comes near vertical
    CHECK(s.nearest_frame > 0.1f);
    CHECK(s.slowest > 1.36f * 0.98f);
    CHECK(s.fastest < 1.36f * 1.02f);
    // Poses the physical body can reach: the joint limits hold, and the feet are still where they were planted
    CHECK(s.report.yielded == 0);
    CHECK(s.report.max_body_limit_excess * deg < 0.1f);
    CHECK(s.report.max_ik_position < 2e-3f);
    CHECK(s.report.max_ik_rotation * deg < 1.f);
    CHECK(s.report.max_slip < 2e-3f);
    CHECK(s.report.max_penetration < 5e-3f);
    CHECK(s.report.max_com_error < 1e-3f);
}

}

TEST_CASE("TW.9 the gait demo on level ground: the track demo's conventions and 1.36 m/s over every stride") {
    if(!haveAssets()) return;
    DemoStats s = recordGait("walker_demo_gait", {}, 6, 150);
    MESSAGE(describe(s));
    checkDemo(s);
    CHECK(s.level == 6);
    // 3 s of warm-up and up to 1.2 s more, drawn per route: the recorded stretches start anywhere in the stride
    CHECK(s.warmup_decisions > 6 * 90);
    CHECK(s.warmup_decisions <= 6 * 126);
    // No jump between two recorded frames: the walk's fastest joint turns 13 degrees per decision
    CHECK(s.axis_step < 0.4f);
    CHECK(s.velocity_step < 2.5f);
    CHECK(s.anchor_speed < 5.f);
}

TEST_CASE("TW.10 the gait demo on the terrain: routes up and down and across at 1.36 m/s inside the joint limits") {
    if(!haveAssets()) return;
    DemoStats s = recordGait("walker_demo_terrain", {}, 40, 150);
    MESSAGE(describe(s));
    checkDemo(s);
    CHECK(s.climbing >= 4);
    CHECK(s.level >= 4);
    CHECK(s.up * deg > 12.f);
    CHECK(s.down * deg > 3.f);
    CHECK(s.across * deg > 6.f);
    CHECK(s.up * deg < 21.f);
    CHECK(s.down * deg < 8.f);
    CHECK(s.across * deg < 13.f);
    // Landings are planned again while the foot swings, as the ground under them shows: a foot changes speed faster than on level ground
    CHECK(s.axis_step < 0.5f);
    CHECK(s.velocity_step < 4.5f);
    CHECK(s.anchor_speed < 6.f);
}

TEST_CASE("TW.11 eval keeps the stretches of an episode: one per target and the last one ends with the episode") {
    if(!haveAssets()) return;
    for(const char* scene : { "walker_eval", "walker_terrain_eval" }) {
        ScenarioRunner runner(scene, { { "num_worlds", "1" } }, SceneLoadOptions {}, 3);
        auto& world = runner.world(0);
        auto& bot = world.reg.ctx().get<HumanBot>();
        auto& io = world.reg.ctx().get<AgentIO>();
        REQUIRE(bot.view_truncate);
        // No policy: zero actions, the ragdoll falls
        REQUIRE(bot.legs.size() == 1);
        CHECK(bot.leg_open);
        glm::vec3 toward = safeNormalize(horizontal(bot.target - bot.spawn_spine));
        CHECK(glm::length(bot.legs[0].direction - toward) < 1e-4f);
        int ticks = 0;
        while(!io.done && ticks < 4000) {
            world.tick();
            ticks++;
        }
        REQUIRE(io.done);
        REQUIRE(!bot.legs.empty());
        CHECK(!bot.leg_open);
        int total = 0;
        for(const WalkLeg& leg : bot.legs) total += leg.ticks;
        CHECK(total == bot.tick);
        CHECK(bot.legs.back().fell == bot.killed);
        CHECK(bot.killed);
        CHECK(glm::length(bot.legs[0].from - bot.spawn_spine) < 1e-5f);
        // On a terrain the walker faces the direction it is sent in, as the flat walker faces -z
        HeadingFrame facing = headingFrame(bot.body->segments[bot.body->spine].spawn_rotation);
        if(bot.routes) CHECK(std::abs(bot.spawn_tries) >= 1);
        else CHECK(glm::length(toward - facing.x) < 1e-3f);
    }
}

TEST_CASE("TW.8 terrain worlds are deterministic and independent of each other") {
    if(!haveAssets()) return;
    auto run = [](uint64_t seed) {
        ScenarioRunner runner("walker_terrain_train", { { "num_worlds", "2" } }, SceneLoadOptions {}, seed);
        for(int t = 0; t < 200; t++) for(size_t w = 0; w < runner.worldCount(); w++) runner.world(w).tick();
        return std::pair<uint64_t, uint64_t>(stateHash(runner.world(0).reg.ctx().get<HumanBot>()), stateHash(runner.world(1).reg.ctx().get<HumanBot>()));
    };
    auto a = run(21), b = run(21), c = run(22);
    CHECK(a == b);
    CHECK(a.first != a.second);
    CHECK(a.first != c.first);
}

}
