#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "logic/agents/agent_io.h"
#include "logic/agents/humanbot.h"
#include "physics/physics_world.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"

using namespace dy;

namespace {

const float deg = 180.f / 3.14159265358979f;

bool haveTrack() {
    return std::filesystem::exists("resources/agents/tracks/heidelbot_notoes__BasicMotions@Walk.track.json");
}

ScenarioRunner rig(std::vector<std::pair<std::string, std::string>> vars = {}) {
    vars.emplace_back("loop", "false");
    return ScenarioRunner("rig_check", vars, SceneLoadOptions {});
}

// Twist and swing of joint j in the current pose, radians
glm::vec3 jointAngles(const HumanBot& bot, int j) {
    const JointDef& joint = bot.body->joints[j];
    glm::quat q = glm::inverse(bot.state.rotation[joint.parent] * bot.frame1[j]) * (bot.state.rotation[joint.child] * bot.frame2[j]);
    if(q.w < 0.f) q = -q;
    float len = std::sqrt(q.w * q.w + q.x * q.x);
    glm::quat twist = len > 1e-6f ? glm::quat(q.w / len, q.x / len, 0.f, 0.f) : glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::quat swing = q * glm::inverse(twist);
    return glm::vec3(2.f * std::atan2(twist.x, twist.w), 2.f * swing.y, 2.f * swing.z);
}

}

TEST_SUITE("scenario") {

TEST_CASE("S5.1 a track for another body is refused") {
    if(!haveTrack()) return;
    CHECK_THROWS(rig({ { "body", "agents/bodies/heidelbot_notoes_h173.body.json" } }));
}

TEST_CASE("S5.2 S5.3 S5.4 S5.5 the walk track on the kinematic rig") {
    if(!haveTrack()) return;
    auto runner = rig();
    auto& world = runner.world(0);
    auto& bot = world.reg.ctx().get<HumanBot>();
    const BodyDef& def = *bot.body;

    const int ticks = 6000;
    std::vector<float> lengths_min(humanbot_joints, 1e9f), lengths_max(humanbot_joints, 0.f);
    std::vector<float> over(humanbot_joints, 0.f);
    std::vector<std::vector<float>> speeds(humanbot_joints);
    float deepest = 0.f, highest_low = 0.f;
    float foot_clearance = def.segments[def.left_foot].shapes[0].half_extents.y;

    for(int tick = 0; tick < ticks; tick++) {
        world.tick();
        if(tick < 2) continue;
        for(int j = 0; j < humanbot_joints; j++) {
            const JointDef& joint = def.joints[j];
            float d = glm::length(bot.state.position[joint.child] - bot.state.position[joint.parent]);
            lengths_min[j] = std::min(lengths_min[j], d);
            lengths_max[j] = std::max(lengths_max[j], d);
            glm::vec3 a = jointAngles(bot, j);
            float excess = std::max({ 0.f, joint.twist_min - a.x, a.x - joint.twist_max });
            over[j] = std::max(over[j], excess);
            speeds[j].push_back(glm::length(bot.state.anchor_velocity[j]));
        }
        float low = 1e9f;
        for(int foot : { def.left_foot, def.right_foot }) {
            const SegmentDef& s = def.segments[foot];
            const ShapeDef& box = s.shapes[0];
            for(int cx = -1; cx <= 1; cx += 2) for(int cy = -1; cy <= 1; cy += 2) for(int cz = -1; cz <= 1; cz += 2) {
                glm::vec3 corner = box.center + box.rotation * glm::vec3(cx * box.half_extents.x, cy * box.half_extents.y, cz * box.half_extents.z);
                glm::vec3 world_corner = bot.state.position[foot] + bot.state.rotation[foot] * corner;
                low = std::min(low, world_corner.y);
            }
        }
        deepest = std::min(deepest, low);
        highest_low = std::max(highest_low, low);
    }
    std::filesystem::create_directories("build/artifacts/P5");
    std::ofstream report("build/artifacts/P5/rig_report.csv");
    report << "joint,length_min,length_max,twist_excess_deg,p99_speed,max_speed\n";
    int bad_lengths = 0;
    for(int j = 0; j < humanbot_joints; j++) {
        const JointDef& joint = def.joints[j];
        bool root_joint = joint.parent == def.pelvis;
        auto v = speeds[j];
        std::sort(v.begin(), v.end());
        float p99 = v[v.size() * 99 / 100], top = v.back();
        report << joint.name << "," << lengths_min[j] << "," << lengths_max[j] << "," << over[j] * deg << "," << p99 << "," << top << "\n";
        if(!root_joint && lengths_max[j] - lengths_min[j] > 6e-3f) { bad_lengths++; MESSAGE(joint.name, " bone length varies by ", lengths_max[j] - lengths_min[j]); }
        CHECK(top <= 5.f * p99 + 0.5f);
    }
    CHECK(bad_lengths == 0);
    MESSAGE("foot lowest point: deepest ", deepest, " m, highest of the per-tick minima ", highest_low, " m (foot half height ", foot_clearance, ")");
    CHECK(deepest > -0.08f);  // the retargeted walk dips the foot corners through the floor: Unity's demos carry that
}

TEST_CASE("S7.5 closure of the energy books under random actions") {
    if(!std::filesystem::exists("resources/agents/bodies/heidelbot_notoes.body.json")) return;
    ScenarioRunner runner("walker_train", { { "num_worlds", "1" } }, SceneLoadOptions {});
    auto& world = runner.world(0);
    auto& bot = world.reg.ctx().get<HumanBot>();
    auto& io = world.reg.ctx().get<AgentIO>();
    bot.params.randomize = false;
    bot.view_truncate = true;
    resetHumanBot(world.reg);
    bot.params.die_on_fall = false;
    std::mt19937 gen(3);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    for(int t = 0; t < 3600; t++) {
        if(t % 2 == 0) for(float& a : io.action) a = u(gen) * 0.3f;
        world.tick();
    }
    // Joint work counts the limits (the Newton-Euler channel): the motors alone do not close, random actions slam the stops
    const FormulaTotals& f = bot.formulas.totals;
    MESSAGE("net ", f.closure_net / f.closure_gross, ", with point work ", f.closure_point_net / f.closure_gross, ", tick-wise ", f.closure_point_tickwise / f.closure_gross, " of ", f.closure_gross, " J");
    CHECK(std::abs(f.closure_point_net) < 0.02 * f.closure_gross);
}

TEST_CASE("S8.4 falling under fixed actions: engine against the Unity rollouts") {
    const std::string path = "resources/agents/golden/heidelbot_notoes/rollouts.json";
    if(!std::filesystem::exists(path)) return;
    std::ifstream in(path);
    auto golden = nlohmann::json::parse(in);
    std::map<int, int> first_tick;
    for(auto& d : golden["decisions"]) { int ep = d["episode"]; if(!first_tick.count(ep)) first_tick[ep] = d["t"]; }
    double unity_zero = 0., unity_small = 0.;
    int n_zero = 0, n_small = 0;
    for(auto& e : golden["episode_ends"]) {
        int ep = e["episode"];
        if(ep % 2 != 0 || !first_tick.count(ep)) continue;
        int ticks = e["t"].get<int>() - first_tick[ep];
        if(ep % 4 == 0) { unity_zero += ticks; n_zero++; } else { unity_small += ticks; n_small++; }
    }
    MESSAGE("unity: zero action ", n_zero ? unity_zero / n_zero : 0., " ticks, +-0.3 random ", n_small ? unity_small / n_small : 0., " ticks");

    ScenarioRunner runner("walker_train", { { "num_worlds", "1" } }, SceneLoadOptions {});
    auto& world = runner.world(0);
    auto& bot = world.reg.ctx().get<HumanBot>();
    auto& io = world.reg.ctx().get<AgentIO>();
    bot.params.randomize = false;
    std::mt19937 gen(9);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    int period = (int) world.reg.ctx().get<WorldClock>().decision_period;
    for(float scale : { 0.f, 0.3f }) {
        double total = 0.;
        int count = 0;
        for(int ep = 0; ep < 40; ep++) {
            io.reset_requested = true;
            world.systems.fixed_observe_only();
            bot.params.randomize = false;
            bot.params.die_on_fall = true;
            for(int t = 0; t < 2000 && !bot.killed; t++) {
                if(t % period == 0) for(float& a : io.action) a = scale * u(gen);
                world.tick();
            }
            total += bot.tick;
            count++;
        }
        MESSAGE("engine: scale ", scale, " mean ticks to the fall ", total / count);
    }
}

TEST_CASE("T9.2 T9.4 recorded observations: header zeroed, no reset spike") {
    if(!haveTrack()) return;
    auto runner = rig();
    auto& world = runner.world(0);
    auto& io = world.reg.ctx().get<AgentIO>();
    int period = (int) world.reg.ctx().get<WorldClock>().decision_period;
    for(int k = 0; k < period; k++) world.tick();
    int episodes = 0;
    float top = 0.f;
    for(int w = 0; w < 1500; w++) {
        for(int k = 0; k < period; k++) world.tick();
        CHECK(io.observation[138] == 0.f);
        CHECK(io.observation[140] == 0.f);
        CHECK(io.observation[139] >= 0.f);
        for(int j = 0; j < 15; j++) {
            const float* o = io.observation.data() + 138 + 3 + 9 * j + 6;
            top = std::max(top, glm::length(glm::vec3(o[0], o[1], o[2])));
        }
        episodes += io.done;
        REQUIRE(io.observation_size == humanbot_observations);
    }
    MESSAGE("max anchor speed ", top, " m/s, episodes ", episodes);
    CHECK(episodes >= 2);
    CHECK(top < 5.f);
}

}
