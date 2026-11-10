#include <doctest/doctest.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>

#include "logic/agents/agent_io.h"
#include "logic/agents/humanbot.h"
#include "physics/physics_world.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"

using namespace dy;

namespace {

const char* const body_path = "resources/agents/bodies/heidelbot_notoes.body.json";
const char* const h173_path = "resources/agents/bodies/heidelbot_notoes_h173.body.json";
const float pi_f = 3.14159265358979f;

bool haveAssets() {
    return std::filesystem::exists(body_path);
}

nlohmann::json readJson(const std::string& path) {
    std::ifstream in(path);
    return nlohmann::json::parse(in);
}

glm::vec3 v3(const nlohmann::json& j) { return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()); }

ScenarioRunner walker(int worlds = 1, std::vector<std::pair<std::string, std::string>> vars = {}) {
    vars.emplace_back("num_worlds", std::to_string(worlds));
    return ScenarioRunner("walker_train", vars, SceneLoadOptions {});
}

HumanBot& botOf(World& w) { return w.reg.ctx().get<HumanBot>(); }

void resetAll(ScenarioRunner& runner) {
    for(size_t i = 0; i < runner.worldCount(); i++) {
        runner.world(i).reg.ctx().get<AgentIO>().reset_requested = true;
        runner.world(i).systems.fixed_observe_only();
    }
}

void freezeRandomisation(HumanBot& bot) {
    bot.params.randomize = false;
}

// The oracle: Unity's observation layout (heidelbot_notoes_1) computed in the engine frame
struct Oracle {
    static glm::vec3 project(glm::quat frame, glm::vec3 v) {
        glm::vec3 f = frame * glm::vec3(0.f, 0.f, -1.f);
        glm::vec3 left = safeNormalize(glm::cross(f, glm::vec3(0.f, 1.f, 0.f)));
        return glm::vec3(glm::dot(f, v), v.y, -glm::dot(left, v));
    }
};

}

TEST_SUITE("unit") {

TEST_CASE("T4.1 imported segment frames are right handed") {
    if(!haveAssets()) return;
    auto body = loadBody(body_path);
    for(auto& s : body->segments) {
        glm::vec3 x = s.rest_rotation * glm::vec3(1, 0, 0), y = s.rest_rotation * glm::vec3(0, 1, 0), z = s.rest_rotation * glm::vec3(0, 0, 1);
        CHECK(glm::dot(glm::cross(x, y), z) > 0.9999f);
    }
}

TEST_CASE("T4.2 imported rest pose against the file") {
    if(!haveAssets()) return;
    auto json = readJson(body_path);
    auto body = parseBody(json);
    for(size_t i = 0; i < body->segments.size(); i++) {
        glm::vec3 p = v3(json["segments"][i]["rest_world_pos"]);
        CHECK(glm::length(unityToEngine(p) - body->segments[i].rest_position) < 1e-5f);
    }
    // The spawn pose differs only below the knees, by the 2.1 degrees
    int knee = body->segmentIndex("LeftLeg"), hip = body->segmentIndex("LeftUpLeg");
    CHECK(glm::length(body->segments[hip].spawn_position - body->segments[hip].rest_position) < 1e-6f);
    float angle = 2.f * std::acos(std::min(1.f, std::abs(glm::dot(body->segments[knee].spawn_rotation, body->segments[knee].rest_rotation))));
    CHECK(angle * 180.f / pi_f == doctest::Approx(2.1f).epsilon(0.02));
}

TEST_CASE("T4.3 joint anchors sit on the child bone origin and T6.2 twist limits are mirrored") {
    if(!haveAssets()) return;
    auto body = loadBody(body_path);
    for(auto& j : body->joints) CHECK(glm::length(j.anchor - body->segments[j.child].rest_position) < 1e-4f);
    auto& knee = body->joints[body->segmentIndex("RightLeg") >= 0 ? 4 : 4];
    CHECK(knee.name == "RightLeg");
    CHECK(knee.twist_min * 180.f / pi_f == doctest::Approx(2.f).epsilon(0.001));
    CHECK(knee.twist_max * 180.f / pi_f == doctest::Approx(132.62f).epsilon(0.001));
    auto& ankle = body->joints[5];
    CHECK(ankle.twist_min * 180.f / pi_f == doctest::Approx(-38.f).epsilon(0.001));
}

TEST_CASE("T4.4 masses are percentages of the total and H173 keeps them at 69 kg") {
    if(!haveAssets()) return;
    auto flat = loadBody(body_path);
    auto h173 = loadBody(h173_path);
    double sum = 0.;
    for(auto& s : flat->segments) sum += s.percentage;
    CHECK(sum == doctest::Approx(99.98).epsilon(0.001));
    CHECK(h173->total_mass == doctest::Approx(69.f));
    CHECK(h173->height == doctest::Approx(1.73f));
    CHECK(flat->segments[0].shapes[0].radius > h173->segments[0].shapes[0].radius);
}

TEST_CASE("T4.5 Jolt mass properties equal the file") {
    if(!haveAssets()) return;
    auto runner = walker(1);
    auto& world = runner.world(0);
    auto& bot = botOf(world);
    auto& physics = world.reg.ctx().get<PhysicsWorld>();
    auto& locks = physics.system().GetBodyLockInterface();
    for(size_t i = 0; i < bot.bodies.size(); i++) {
        JPH::BodyLockRead lock(locks, bot.bodies[i]);
        REQUIRE(lock.Succeeded());
        auto* mp = lock.GetBody().GetMotionProperties();
        const auto& seg = bot.body->segments[i];
        CHECK(1.f / mp->GetInverseMass() == doctest::Approx(bot.params.total_mass * seg.percentage / 100.f).epsilon(1e-4));
        glm::vec3 com = toGlm(lock.GetBody().GetCenterOfMassPosition() - lock.GetBody().GetPosition());
        glm::vec3 expected = bot.state.rotation[i] * seg.center_of_mass;
        CHECK(glm::length(com - expected) < 1e-4f);
    }
}

TEST_CASE("T4.6 no pair of segments of a ragdoll collides") {
    if(!haveAssets()) return;
    auto runner = walker(1);
    auto& bot = botOf(runner.world(0));
    auto& physics = runner.world(0).reg.ctx().get<PhysicsWorld>();
    auto& locks = physics.system().GetBodyLockInterface();
    JPH::BodyLockRead a(locks, bot.bodies[0]), b(locks, bot.bodies[1]);
    REQUIRE(a.Succeeded());
    REQUIRE(b.Succeeded());
    CHECK_FALSE(a.GetBody().GetCollisionGroup().CanCollide(b.GetBody().GetCollisionGroup()));
}

TEST_CASE("T4.7 a malformed body file is refused") {
    if(!haveAssets()) return;
    auto base = readJson(body_path);
    auto refuse = [&](auto mutate) {
        auto j = base;
        mutate(j);
        CHECK_THROWS_AS(parseBody(j), AssetError);
    };
    refuse([](nlohmann::json& j) { j["format_version"] = 99; });
    refuse([](nlohmann::json& j) { j["frame"] = "engine"; });
    refuse([](nlohmann::json& j) { j["segments"][3]["body_percentage"] = 40.f; });
    refuse([](nlohmann::json& j) { j["segments"][2]["parent"] = "nobody"; });
    refuse([](nlohmann::json& j) { j["segments"][2]["shapes"] = nlohmann::json::array(); });
    refuse([](nlohmann::json& j) { j["segments"][2]["inertia_tensor"] = { 0, 0, 0 }; });
    refuse([](nlohmann::json& j) { j["joints"][2]["parent"] = "nobody"; });
    refuse([](nlohmann::json& j) { j["joints"][2]["anchor_world"] = { 9, 9, 9 }; });
    refuse([](nlohmann::json& j) { j["joints"].erase(0); });
    refuse([](nlohmann::json& j) { j["segments"][4]["role"] = "pelvis2"; j["segments"][0]["role"] = ""; });
}

TEST_CASE("T4.8 spawn clearance matches Unity's") {
    if(!haveAssets()) return;
    auto runner = walker(1);
    auto& world = runner.world(0);
    auto& bot = botOf(world);
    auto& physics = world.reg.ctx().get<PhysicsWorld>();
    auto& locks = physics.system().GetBodyLockInterface();
    float lowest = 1e9f;
    for(size_t i = 0; i < bot.bodies.size(); i++) {
        JPH::BodyLockRead lock(locks, bot.bodies[i]);
        REQUIRE(lock.Succeeded());
        // The straight rest pose, as Unity's spawn: the file's value is taken there
        const auto& seg = bot.body->segments[i];
        JPH::RMat44 t = JPH::RMat44::sRotationTranslation(toJolt(seg.rest_rotation), JPH::RVec3(toJolt(seg.rest_position + seg.rest_rotation * seg.center_of_mass)));
        JPH::AABox box = lock.GetBody().GetShape()->GetWorldSpaceBounds(t, JPH::Vec3::sReplicate(1.f));
        lowest = std::min(lowest, (float) box.mMin.GetY());
    }
    CHECK(std::abs(lowest - bot.body->unity_lowest_y) < 0.02f);
}

TEST_CASE("T6.3 action zero is mid twist and full deflection the limits") {
    if(!haveAssets()) return;
    auto runner = walker(1);
    auto& world = runner.world(0);
    auto& io = world.reg.ctx().get<AgentIO>();
    auto& bot = botOf(world);
    std::fill(io.action.begin(), io.action.end(), 0.f);
    DriveTargetsSys drive(world.reg);
    drive.tick(1. / 60.);
    auto* knee = bot.constraints[4].GetPtr();
    glm::quat q = toGlm(knee->GetTargetOrientationCS());
    float angle = 2.f * std::atan2(q.x, q.w) * 180.f / pi_f;
    CHECK(angle == doctest::Approx(67.31f).epsilon(0.002));
    io.action[8 * 4 + 2] = 1.f;
    drive.tick(1. / 60.);
    q = toGlm(knee->GetTargetOrientationCS());
    CHECK(2.f * std::atan2(q.x, q.w) * 180.f / pi_f == doctest::Approx(132.62f).epsilon(0.001));
    io.action[8 * 4 + 2] = -1.f;
    drive.tick(1. / 60.);
    q = toGlm(knee->GetTargetOrientationCS());
    CHECK(2.f * std::atan2(q.x, q.w) * 180.f / pi_f == doctest::Approx(2.f).epsilon(0.005));
}

TEST_CASE("T8.1 heading frames are orthogonal and yaw only") {
    std::mt19937 gen(5);
    std::normal_distribution<float> n;
    for(int i = 0; i < 1000; i++) {
        glm::quat q = glm::normalize(glm::quat(n(gen), n(gen), n(gen), n(gen)));
        if(i % 3 == 0) q = glm::normalize(glm::angleAxis(pi_f * 0.5f, glm::vec3(1, 0, 0)) * glm::quat(1.f, 0.001f * n(gen), 0.001f * n(gen), 0.f));
        HeadingFrame h = headingFrame(q);
        CHECK(std::isfinite(h.x.x));
        CHECK(std::abs(glm::dot(h.x, h.y)) < 1e-5f);
        CHECK(std::abs(glm::dot(h.x, h.z)) < 1e-5f);
        CHECK(std::abs(glm::dot(h.y, h.z)) < 1e-5f);
        CHECK(h.y.y == doctest::Approx(1.f));
        CHECK(h.x.y == doctest::Approx(0.f));
        CHECK(glm::length(h.x) == doctest::Approx(1.f).epsilon(1e-5));
    }
}

TEST_CASE("T8.8 a zero target speed gives no NaN and a root reward of exactly zero") {
    CHECK(glm::length(safeNormalize(glm::vec3(0.f))) == 0.f);
    if(!haveAssets()) return;
    auto runner = walker(1);
    auto& world = runner.world(0);
    auto& bot = botOf(world);
    auto& io = world.reg.ctx().get<AgentIO>();
    bot.target_speed = 0.f;
    bot.snapshot.target_speed = 0.f;
    bot.snapshot.tsv = glm::vec3(0.f);
    bot.params.randomize = false;
    world.tick();
    world.tick();
    for(float f : io.observation) CHECK(std::isfinite(f));
    CHECK(io.reward == doctest::Approx(0.f));
}

}

TEST_SUITE("scenario") {

TEST_CASE("S8.1 the oracle layout reproduces Unity's observations and S8.2 root rewards") {
    const std::string path = "resources/agents/golden/heidelbot_notoes/rollouts.json";
    if(!std::filesystem::exists(path)) return;
    auto body = loadBody(body_path);
    auto golden = readJson(path);
    float est0 = golden["est0"].get<float>();
    float dt = golden["dt"].get<float>() * golden["decision_period"].get<int>();
    const auto& decisions = golden["decisions"];
    CHECK(decisions.size() > 100);

    float worst = 0.f;
    int worst_index = -1, worst_t = -1;
    int compared = 0, rewards = 0;
    float worst_reward = 0.f;

    for(size_t k = 0; k < decisions.size(); k++) {
        const auto& d = decisions[k];
        const auto& poses = d["poses"];
        auto pos = [&](int i) { return unityToEngine(glm::vec3(poses[i][0].get<float>(), poses[i][1].get<float>(), poses[i][2].get<float>())); };
        auto rot = [&](int i) { return glm::normalize(unityToEngine(glm::quat(poses[i][6].get<float>(), poses[i][3].get<float>(), poses[i][4].get<float>(), poses[i][5].get<float>()))); };

        glm::vec3 spine = pos(body->spine);
        float height = std::max({ pos(body->head).y - pos(body->left_foot).y, pos(body->head).y - pos(body->right_foot).y, 0.f });
        float target_speed = d["target_speed"].get<float>();
        glm::vec3 target = unityToEngine(v3(d["target"]));
        glm::vec3 tsv = safeNormalize(horizontal(target - spine)) * target_speed;
        bool dying = height < 0.7f * est0;
        if(dying) tsv = glm::vec3(0.f);
        glm::vec3 diff = tsv - horizontal(unityToEngine(v3(d["spine_rb_velocity"])));
        diff.y = std::clamp(1.f - height / est0, 0.f, 1.f);

        std::array<float, 138> o;
        glm::vec3 h = Oracle::project(rot(body->spine), diff);
        o[0] = h.x; o[1] = h.y; o[2] = h.z;
        for(int j = 0; j < 15; j++) {
            const JointDef& joint = body->joints[j];
            float sign = joint.side == 'R' ? -1.f : 1.f;
            glm::quat frame = rot(body->frameSegment(j));
            glm::vec3 n = Oracle::project(frame, rot(joint.child) * (joint.unity_axis * sign));
            glm::vec3 t = Oracle::project(frame, rot(joint.child) * (joint.unity_secondary * sign));
            glm::vec3 u = Oracle::project(frame, unityToEngine(v3(d["real_velocity"][j])));
            for(int c = 0; c < 3; c++) { o[3 + 9 * j + c] = n[c]; o[6 + 9 * j + c] = t[c]; o[9 + 9 * j + c] = u[c]; }
        }
        const auto& logged = d["obs"];
        // Unity quirk: the first observation after a kill still reads the stale dying flag, so its command is zero
        for(int i = d["decision"].get<int>() == 0 ? 3 : 0; i < 138; i++) {
            float error = std::abs(o[i] - logged[138 + i].get<float>());
            if(error > worst) { worst = error; worst_index = i; worst_t = d["t"].get<int>(); }
        }
        compared++;

        // The reward of the window that just ended, against the previous decision's command
        if(k > 0 && d["episode"] == decisions[k - 1]["episode"] && d["decision"].get<int>() == decisions[k - 1]["decision"].get<int>() + 1) {
            const auto& e = decisions[k - 1];
            glm::vec3 previous_spine = unityToEngine(glm::vec3(e["poses"][body->spine][0].get<float>(), e["poses"][body->spine][1].get<float>(), e["poses"][body->spine][2].get<float>()));
            float speed = e["target_speed"].get<float>();
            glm::vec3 previous_tsv = safeNormalize(horizontal(unityToEngine(v3(e["target"])) - previous_spine)) * speed;
            glm::vec3 velocity = (spine - previous_spine) / dt;
            float root = std::min(glm::dot(velocity, safeNormalize(previous_tsv)), speed) * 0.34f;
            if(speed > 0.f) root /= speed;
            float expected = dying ? 0.f : root;
            float actual = d["cumulative_reward"].get<float>() - e["cumulative_reward"].get<float>();
            if(std::abs(expected - actual) > worst_reward) worst_reward = std::abs(expected - actual);
            rewards++;
        }
    }
    MESSAGE("compared ", compared, " decisions, worst error ", worst, " at element ", worst_index, " tick ", worst_t, ", rewards ", rewards, " worst ", worst_reward);
    CHECK(worst < 2e-3f);
    CHECK(worst_reward < 1e-3f);
}

TEST_CASE("S4.1 a dynamic ragdoll without gravity keeps its pose") {
    if(!haveAssets()) return;
    PhysicsConfig unused;
    auto runner = walker(1);
    auto& world = runner.world(0);
    auto& physics = world.reg.ctx().get<PhysicsWorld>();
    physics.system().SetGravity(JPH::Vec3::sZero());
    auto& bot = botOf(world);
    bot.params.randomize = false;
    resetHumanBot(world.reg);
    auto before = bot.state.position;
    BodySyncSys sync(world.reg);
    for(int i = 0; i < 1000; i++) physics.step(1.f / 60.f);
    sync.tick(1. / 60.);
    float drift = 0.f;
    for(size_t i = 0; i < 16; i++) drift = std::max(drift, glm::length(bot.state.position[i] - before[i]));
    MESSAGE("drift ", drift);
    CHECK(drift < 1e-3f);
}

TEST_CASE("S4.3 a walker episode runs: no NaN, kills, resets, ids stable") {
    if(!haveAssets()) return;
    auto runner = walker(4);
    resetAll(runner);
    std::mt19937 gen(7);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    int kills = 0, resets = 0;
    uint32_t period = runner.world(0).reg.ctx().get<WorldClock>().decision_period;
    for(int window = 0; window < 2500; window++) {
        for(size_t w = 0; w < runner.worldCount(); w++) {
            auto& io = runner.world(w).reg.ctx().get<AgentIO>();
            for(float& a : io.action) a = u(gen);
        }
        runner.step(period);
        for(size_t w = 0; w < runner.worldCount(); w++) {
            auto& io = runner.world(w).reg.ctx().get<AgentIO>();
            for(float f : io.observation) REQUIRE(std::isfinite(f));
            REQUIRE(std::isfinite(io.reward));
            if(io.done) {
                kills += !io.truncated;
                resets++;
            }
        }
    }
    MESSAGE("episodes ", resets, " kills ", kills);
    CHECK(resets > 4);
    CHECK(kills > 0);
}

TEST_CASE("Energy formulas: Newton-Euler recovers Jolt's impulses, the motor axes are Jolt's, the Lagrangian matches the bodies") {
    if(!haveAssets()) return;
    auto runner = walker(1);
    resetAll(runner);
    auto& world = runner.world(0);
    auto& bot = botOf(world);
    auto& io = world.reg.ctx().get<AgentIO>();
    uint32_t period = world.reg.ctx().get<WorldClock>().decision_period;
    runner.step(period);
    bot.view_truncate = true;
    bot.params.stats_settle_seconds = 0.f;
    std::mt19937 gen(3);
    std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    auto& physics = physicsWorld(world.reg);
    auto lagrangian = [&]() {
        double L = 0.;
        for(auto id : bot.bodies) {
            JPH::BodyLockRead lock(physics.system().GetBodyLockInterfaceNoLock(), id);
            const JPH::Body& b = lock.GetBody();
            const JPH::MotionProperties* mp = b.GetMotionProperties();
            double m = 1. / mp->GetInverseMass();
            JPH::Vec3 v = b.GetLinearVelocity(), w = b.GetAngularVelocity();
            JPH::Vec3 Iw = b.GetInverseInertia().Inversed3x3().Multiply3x3(w);
            L += 0.5 * m * v.Dot(v) + 0.5 * w.Dot(Iw) - m * 9.81 * b.GetCenterOfMassPosition().GetY();
        }
        return L;
    };
    double gravity_ratio = -physics.config().gravity.y / 9.81;
    REQUIRE(gravity_ratio == doctest::Approx(1.).epsilon(1e-5));
    double independent = 0., previous = 0.;
    int ticks = 0;
    for(int window = 0; window < 30 && !bot.killed; window++) {
        for(float& a : io.action) a = u(gen);
        for(uint32_t i = 0; i < period; i++) {
            runner.step(1);
            double now = lagrangian();
            if(ticks > 0) independent += std::abs(now - previous);
            previous = now;
            ticks++;
        }
    }
    const FormulaTotals& t = bot.formulas.totals;
    REQUIRE(t.ticks > 20);
    MESSAGE("force ", t.point_error / t.point_reference, " torque ", t.torque_error_free / t.torque_reference_free, " contact ", t.contact_error / t.contact_reference);
    CHECK(t.point_error / t.point_reference < 1e-5);
    CHECK(t.torque_error_free / t.torque_reference_free < 2e-3);
    CHECK(t.contact_error / t.contact_reference < 1e-5);
    CHECK(t.root_linear / t.weight_impulse < 1e-5);
    CHECK(t.lagrangian_abs == doctest::Approx(independent).epsilon(5e-3));
}

TEST_CASE("The torque-rate term sums the motor torque changes over the window") {
    if(!haveAssets()) return;
    auto runner = walker(1);
    resetAll(runner);
    auto& world = runner.world(0);
    auto& bot = botOf(world);
    auto& io = world.reg.ctx().get<AgentIO>();
    uint32_t period = world.reg.ctx().get<WorldClock>().decision_period;
    runner.step(period);
    std::mt19937 gen(5);
    std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    for(float& a : io.action) a = u(gen);
    runner.step(period);
    std::array<glm::vec3, humanbot_joints> previous;
    for(int j = 0; j < humanbot_joints; j++) previous[j] = bot.books.joints[j].torque;
    float expected = 0.f;
    for(int tick = 0; tick < 3; tick++) {
        float before = bot.books.window_torque_variation;
        runner.step(1);
        float sum = 0.f;
        for(int j = 0; j < humanbot_joints; j++) {
            sum += glm::length(bot.books.joints[j].torque - previous[j]);
            previous[j] = bot.books.joints[j].torque;
        }
        CHECK(bot.books.tick_torque_variation == doctest::Approx(sum).epsilon(1e-5));
        expected = sum;
        (void) before;
    }
    CHECK(expected > 0.f);
}

TEST_CASE("A DeVita stance window runs from the first loaded tick to the last") {
    StanceWindow<SagittalWork> window;
    float value = 0.f;
    int emitted = 0, emitted_ticks = 0, emitted_start = -1;
    SagittalWork total;
    // off off | on x5, off x2 (flicker), on x4 | off x3 | off
    const bool pattern[] = { false, false, true, true, true, true, true, false, false, true, true, true, true, false, false, false, false };
    for(int tick = 0; tick < (int) std::size(pattern); tick++) {
        value = (float) (tick + 1);
        window.step(pattern[tick], tick, 1.f / 60.f, 3, 2.f, [&](SagittalWork& w) { w.pos += value; },
            [&](const SagittalWork& w, int ticks, int start) { emitted++; emitted_ticks = ticks; emitted_start = start; total = w; });
    }
    CHECK(emitted == 1);
    CHECK(emitted_ticks == 11);     // ticks 2 to 12, the flicker included, toe-off's debounce dropped
    CHECK(emitted_start == 2);
    CHECK(total.pos == doctest::Approx(3.f + 4.f + 5.f + 6.f + 7.f + 8.f + 9.f + 10.f + 11.f + 12.f + 13.f));
}

TEST_CASE("DeVita's episode stats leave with the reset") {
    if(!haveAssets()) return;
    auto runner = walker(1);
    resetAll(runner);
    auto& reg = runner.world(0).reg;
    auto& bot = reg.ctx().get<HumanBot>();
    auto& io = reg.ctx().get<AgentIO>();
    bot.devita.sum_pos = 10.f;
    bot.devita.sum_neg = 5.f;
    bot.devita.stances = 4;
    io.reset_requested = true;
    runner.step(reg.ctx().get<WorldClock>().decision_period);
    bool found = false;
    for(const auto& s : io.stats) {
        if(std::string(s.key) == "DeVita/NegPos ratio") {
            found = true;
            CHECK(s.value == doctest::Approx(0.5f));
        }
    }
    CHECK(found);
    CHECK(bot.devita.stances == 0);
    CHECK(bot.devita.total_stances == 4);
}

}
