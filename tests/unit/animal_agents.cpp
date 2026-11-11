#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "logic/agents/agent_io.h"
#include "logic/animals/animal_agent.h"
#include "logic/animals/animal_body.h"
#include "logic/agents/ground_field.h"
#include "logic/agents/walk_routes.h"
#include "physics/physics_sys.h"
#include "physics/physics_world.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"
#include "util/paths.h"

using namespace dy;

// The physical animals (docs/physical-agents/reports/animal-agents.md): generated bodies, the agent, the
// demonstrations. Tests on the generated library and its bodies (tools/animal_body) are skipped without them

namespace {

using Vars = std::vector<std::pair<std::string, std::string>>;

std::string bodyOf(const std::string& species) {
    return libraryResourceFile("animals/bodies/" + species + ".body.json").string();
}

bool haveBody(const std::string& species) {
    return std::filesystem::exists(bodyOf(species)) && std::filesystem::exists(animalsDirectory() / "library.json");
}

uint64_t stateHash(const AnimalBot& bot) {
    uint64_t h = 1469598103934665603ull;
    for(size_t i = 0; i < bot.position.size(); i++) {
        for(int k = 0; k < 3; k++) {
            uint32_t bits;
            std::memcpy(&bits, &bot.position[i][k], 4);
            h = (h ^ bits) * 1099511628211ull;
        }
    }
    return h;
}

// A deer, a bird on two legs, a man: three bodies that differ
const char* const three[] = { "deer_red", "crow", "homo_heidelbergensis_walking" };

// An action that depends on the tick and the slot only
void wiggle(AgentIO& io, int tick) {
    for(size_t a = 0; a < io.action.size(); a++) io.action[a] = 0.3f * std::sin(0.37f * (float) a + 0.11f * (float) tick);
}

}

TEST_SUITE("unit") {

TEST_CASE("AA.1 the joint angles of the body file are those Jolt measures and the standing pose is inside every limit") {
    for(const char* species : three) {
        if(!haveBody(species)) continue;
        ScenarioRunner runner("animal_stand", { { "body", bodyOf(species) } }, SceneLoadOptions {}, 1);
        auto& bot = runner.world(0).reg.ctx().get<AnimalBot>();
        const AnimalBody& body = *bot.body;
        REQUIRE(bot.built);
        for(size_t j = 0; j < body.def.joints.size(); j++) {
            const JointDef& joint = body.def.joints[j];
            glm::quat mine = glm::inverse(body.frame1[j]) * glm::inverse(bot.rotation[joint.parent]) * bot.rotation[joint.child] * body.frame2[j];
            glm::quat jolt = toGlm(bot.parts.constraints[j]->GetRotationInConstraintSpace());
            CAPTURE(std::string(species));
            CAPTURE(joint.name);
            CHECK(std::abs(glm::dot(glm::normalize(mine), jolt)) > 0.9999f);
            JointAngles angles = jointAngles(body, (int) j, bot.rotation[joint.parent], bot.rotation[joint.child]);
            CHECK(limitExcess(body, (int) j, angles) == doctest::Approx(0.f).epsilon(1e-4));
            // A zero action asks for the standing pose
            glm::quat stand = constraintRotation(body.drives[j].stand.x, body.drives[j].stand.y, body.drives[j].stand.z);
            CHECK(std::abs(glm::dot(stand, jolt)) > 0.99999f);
        }
    }
}

TEST_CASE("AA.2 a body stands 10 s on its drives with a zero action and falls limp") {
    for(const char* species : three) {
        if(!haveBody(species)) continue;
        AnimalStandResult stand = animalStandTest(bodyOf(species), 10.f, false);
        CAPTURE(std::string(species));
        CAPTURE(stand.trunk_drop);
        CAPTURE(stand.drift);
        CAPTURE(stand.tilt);
        CAPTURE(stand.max_speed);
        CAPTURE(stand.fall_segment);
        CHECK(stand.finite);
        CHECK(stand.stands);
        AnimalStandResult limp = animalStandTest(bodyOf(species), 10.f, true);
        CHECK(limp.fell);
        CHECK(limp.rests);
    }
}

TEST_CASE("AA.3 the layout is the body's: a header, nine values for the trunk and for each joint, stacked twice; eight actions a joint") {
    for(const char* species : three) {
        if(!haveBody(species)) continue;
        ScenarioRunner runner("animal_train", { { "body", bodyOf(species) }, { "num_worlds", "1" } }, SceneLoadOptions {}, 3);
        auto& reg = runner.world(0).reg;
        auto& bot = reg.ctx().get<AnimalBot>();
        auto& io = reg.ctx().get<AgentIO>();
        const int segments = (int) bot.body->def.segments.size();
        CHECK(io.behavior == "Animal_" + std::string(species));
        CHECK(io.observation_size == 2 * (animal_header + 9 * segments));
        CHECK(io.action_size == 8 * (segments - 1));
        CHECK(segments >= 15);
        CHECK(segments <= 25);
        // The first observation of an episode: an empty older half, the standing animal in the newer one
        runner.world(0).systems.fixed_observe_only();
        for(int i = 0; i < bot.frame_size; i++) CHECK(io.observation[i] == 0.f);
        const float* frame = io.observation.data() + bot.frame_size;
        // The command, in walk speeds, ahead of the trunk (the episode starts facing it)
        CHECK(frame[0] == doctest::Approx(bot.command.speed / bot.body->walk_speed).epsilon(0.02));
        CHECK(std::abs(frame[1]) < 0.05f);
        CHECK(std::abs(frame[5]) < 0.01f);
        // The trunk's forward and up axes in its own heading frame
        CHECK(frame[animal_header] == doctest::Approx(1.f).epsilon(0.02));
        CHECK(frame[animal_header + 4] == doctest::Approx(1.f).epsilon(0.02));
    }
}

TEST_CASE("AA.4 a training world of a quadruped steps deterministically, and its worlds do not depend on each other") {
    if(!haveBody("deer_red")) return;
    auto run = [](const char* worlds, uint64_t seed, std::vector<uint64_t>& hashes) {
        ScenarioRunner runner("animal_train", { { "body", bodyOf("deer_red") }, { "num_worlds", worlds } }, SceneLoadOptions {}, seed);
        hashes.assign(runner.worldCount(), 0);
        for(int t = 0; t < 400; t++) {
            for(size_t w = 0; w < runner.worldCount(); w++) {
                auto& reg = runner.world(w).reg;
                wiggle(reg.ctx().get<AgentIO>(), t);
                runner.world(w).tick();
                hashes[w] = hashes[w] * 31 + stateHash(reg.ctx().get<AnimalBot>());
            }
        }
    };
    std::vector<uint64_t> a, b, c, alone;
    run("2", 7, a);
    run("2", 7, b);
    run("2", 8, c);
    run("1", 7, alone);
    CHECK(a[0] == b[0]);
    CHECK(a[1] == b[1]);
    CHECK(a[0] != a[1]);
    CHECK(a[0] != c[0]);
    CHECK(a[0] == alone[0]);
}

TEST_CASE("AA.5 the fall test both ways: the limp ragdoll is ended, the kinematic walk across its ranges never is") {
    for(const char* species : three) {
        if(!haveBody(species)) continue;
        CAPTURE(std::string(species));
        {
            ScenarioRunner runner("animal_train", { { "body", bodyOf(species) }, { "num_worlds", "1" } }, SceneLoadOptions {}, 2);
            auto& world = runner.world(0);
            auto& bot = world.reg.ctx().get<AnimalBot>();
            auto& io = world.reg.ctx().get<AgentIO>();
            bot.limp = true;
            int ended = -1;
            for(int t = 0; t < 600 && ended < 0; t++) {
                world.tick();
                if(io.done) ended = t;
            }
            CHECK(ended >= 0);
            CHECK(bot.killed);
            CHECK(!io.truncated);
            // The terminal window carries the death penalty of the params in use, whatever its value
            CHECK(io.reward <= bot.params.death_penalty + bot.params.root_reward + 1e-3f);
            CHECK(io.reward >= bot.params.death_penalty - bot.params.root_reward - 1e-3f);
        }
        for(const char* ranges : { "agents/ranges/walk_basic.json", "agents/ranges/walk_style.json", "agents/ranges/walk_soft.json" }) {
            CAPTURE(std::string(ranges));
            ScenarioRunner runner("animal_demo", { { "body", bodyOf(species) }, { "ranges", ranges }, { "episode_decisions", "150" } }, SceneLoadOptions {}, 5);
            runner.step(60 * 40);
            const auto& checks = runner.world(0).reg.ctx().get<AnimalDemoChecks>();
            CHECK(checks.ticks > 1000);
            CAPTURE(checks.fall_cause);
            CAPTURE(checks.fall_segment);
            CHECK(checks.falls == 0);
        }
    }
}

TEST_CASE("AA.6 a recorded walk on the body: limits hold on every tick, the trunk's heading never flips, the speed the reward measures is the commanded one") {
    for(const char* species : three) {
        if(!haveBody(species)) continue;
        CAPTURE(std::string(species));
        ScenarioRunner runner("animal_demo", { { "body", bodyOf(species) }, { "episode_decisions", "200" } }, SceneLoadOptions {}, 9);
        auto& world = runner.world(0);
        auto& bot = world.reg.ctx().get<AnimalBot>();
        auto& io = world.reg.ctx().get<AgentIO>();
        const auto& walk = world.reg.ctx().get<AnimalWalkState>();
        REQUIRE(walk.ready);
        int recorded = 0, episodes = 0, first_frames = 0;
        float worst_heading = 0.f;
        bool finite = true, empty_start = true;
        for(int t = 0; t < 60 * 60; t++) {
            world.tick();
            if(t % 2 != 1 || !io.recordable) continue;
            // An episode starts on an empty stack
            if(bot.recorded == 1) {
                first_frames++;
                for(int i = 0; i < bot.frame_size; i++) if(io.observation[i] != 0.f) empty_start = false;
            }
            recorded++;
            episodes += io.done ? 1 : 0;
            for(float v : io.observation) if(!std::isfinite(v)) finite = false;
            // The command of the observation is the one the walk follows: its heading is the walker's
            worst_heading = std::max(worst_heading, std::abs(std::remainder(bot.command.yaw - walk.walk.walker.yaw, 6.2831853f)));
        }
        const auto& checks = world.reg.ctx().get<AnimalDemoChecks>();
        CHECK(finite);
        CHECK(empty_start);
        CHECK(recorded > 1200);
        CHECK(episodes >= 6);
        CHECK(first_frames >= episodes);
        CHECK(worst_heading < 1e-4f);
        CAPTURE(checks.limit_joint);
        CHECK(checks.limit_ticks == 0);
        CHECK(glm::degrees(checks.limit_excess) < 0.5f);
        CHECK(checks.heading_flips == 0);
        CHECK(checks.falls == 0);
        CHECK(checks.commanded > 0.);
        // As the reward measures it: the trunk's progress along the commanded heading, per decision
        CHECK(checks.progress / checks.commanded == doctest::Approx(1.).epsilon(0.03));
        CHECK(checks.speed_error < 0.05f);
    }
}

TEST_CASE("AA.7 a run goes through the same recorder as the walk") {
    if(!haveBody("deer_red")) return;
    ScenarioRunner runner("animal_demo", { { "body", bodyOf("deer_red") }, { "gait", "run" }, { "episode_decisions", "100" } }, SceneLoadOptions {}, 4);
    const auto& walk = runner.world(0).reg.ctx().get<AnimalWalkState>();
    REQUIRE(walk.ready);
    runner.step(60 * 15);
    const auto& checks = runner.world(0).reg.ctx().get<AnimalDemoChecks>();
    CHECK(checks.decisions > 200);
    CHECK(checks.commanded > 0.);
    CHECK(checks.progress / checks.commanded == doctest::Approx(1.).epsilon(0.1));
}

TEST_CASE("AA.8 the terrain worlds load, spawn on routes in the slope band and record") {
    if(!haveBody("deer_red") || !std::filesystem::exists("resources/mclookup.bin")) return;
    {
        ScenarioRunner runner("animal_terrain_train", { { "body", bodyOf("deer_red") }, { "num_worlds", "2" } }, SceneLoadOptions {}, 6);
        for(size_t w = 0; w < runner.worldCount(); w++) {
            auto& bot = runner.world(w).reg.ctx().get<AnimalBot>();
            REQUIRE(bot.ground);
            REQUIRE(bot.routes);
            // On its soles on the ground, at its standing height, the command along a route in the band
            CHECK(bot.trunkHeight() == doctest::Approx(bot.body->stand_height).epsilon(0.08));
            CHECK(bot.fallen() == 0);
            glm::vec3 from = bot.position[0];
            RouteSlope slope = measureRoute(*bot.ground, glm::vec3(from.x, bot.groundUnder(from), from.z), bot.command.direction(), 6.f, 0.5f);
            CHECK(slope.ground);
            CHECK(inBand(slope, *bot.routes));
        }
        // Limp on the colliders: it falls on the terrain and is ended, the next episode is elsewhere
        auto& world = runner.world(0);
        auto& bot = world.reg.ctx().get<AnimalBot>();
        auto& io = world.reg.ctx().get<AgentIO>();
        const glm::vec3 first = bot.origin;
        bot.limp = true;
        int ended = -1;
        float lowest = 1e9f;
        for(int t = 0; t < 600 && ended < 0; t++) {
            world.tick();
            for(size_t s = 0; s < bot.position.size(); s++) lowest = std::min(lowest, bot.position[s].y - bot.groundUnder(bot.position[s] + glm::vec3(0.f, 1.f, 0.f)));
            if(io.done) ended = t;
        }
        CHECK(ended >= 0);
        CHECK(bot.killed);
        CHECK(lowest > -0.3f);
        world.tick();
        world.tick();
        CHECK(glm::distance(bot.origin, first) > 1.f);
    }
    {
        ScenarioRunner runner("animal_demo_terrain", { { "body", bodyOf("deer_red") }, { "episode_decisions", "150" } }, SceneLoadOptions {}, 3);
        runner.step(60 * 30);
        const auto& checks = runner.world(0).reg.ctx().get<AnimalDemoChecks>();
        CHECK(checks.decisions > 400);
        CHECK(checks.limit_ticks == 0);
        CHECK(checks.falls == 0);
        CHECK(checks.heading_flips == 0);
        CHECK(checks.progress / checks.commanded == doctest::Approx(1.).epsilon(0.05));
    }
}

TEST_CASE("AA.9 soft ground: the depth is in the observation, the animal stands in it, the layer drags more sideways than upwards and nothing downwards") {
    if(!haveBody("deer_red")) return;
    ScenarioRunner runner("animal_stand", { { "body", bodyOf("deer_red") }, { "soft_depth", "0.08" } }, SceneLoadOptions {}, 1);
    auto& world = runner.world(0);
    auto& bot = world.reg.ctx().get<AnimalBot>();
    auto& io = world.reg.ctx().get<AgentIO>();
    bot.immortal = true;
    CHECK(bot.soft_depth == doctest::Approx(0.08f * bot.body->height));
    for(int t = 0; t < 300; t++) world.tick();
    CHECK(io.observation[bot.frame_size + 6] == doctest::Approx(0.08f));
    CHECK(bot.fallen() == 0);
    CHECK(std::abs(bot.trunkHeight() / bot.body->stand_height - 1.f) < 0.08f);
    // The layer: a drag against the velocity, more sideways than upwards, none on the way down, zero at the surface,
    // and never more than what stops the body within the step
    const AnimalParams& p = bot.params;
    const float mass = 5.f, walk = bot.body->walk_speed, h = 1.f / 60.f;
    glm::vec3 aside = softGroundForce(p, mass, walk, 0.5f, glm::vec3(0.01f, 0.f, 0.f), h);
    glm::vec3 up = softGroundForce(p, mass, walk, 0.5f, glm::vec3(0.f, 0.01f, 0.f), h);
    glm::vec3 down = softGroundForce(p, mass, walk, 0.5f, glm::vec3(0.f, -0.01f, 0.f), h);
    CHECK(aside.x < 0.f);
    CHECK(up.y < 0.f);
    CHECK(std::abs(up.y) == doctest::Approx(p.soft_lift * std::abs(aside.x)));
    CHECK(down == glm::vec3(0.f));
    CHECK(softGroundForce(p, mass, walk, 0.f, glm::vec3(1.f, 0.f, 0.f), h) == glm::vec3(0.f));
    glm::vec3 fast = softGroundForce(p, mass, walk, 1.f, glm::vec3(3.f, 0.f, 0.f), h);
    CHECK(std::abs(fast.x) * h / mass <= 3.f * 1.0001f);
    // A limp animal dropped in the layer comes to rest: the drag does not feed it
    bot.limp = true;
    for(int t = 0; t < 600; t++) world.tick();
    float fastest = 0.f;
    for(size_t s = 0; s < bot.linear.size(); s++) fastest = std::max(fastest, glm::length(bot.linear[s]));
    CHECK(std::isfinite(fastest));
    CHECK(fastest < 0.2f * std::sqrt(9.81f * bot.body->com_height));
}

}
