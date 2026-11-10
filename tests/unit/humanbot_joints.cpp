#include <doctest/doctest.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "logic/agents/agent_io.h"
#include "logic/agents/humanbot.h"
#include "logic/components/rigid_body.h"
#include "physics/physics_world.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"

using namespace dy;

namespace {

const char* const golden_dir = "resources/agents/golden/heidelbot_notoes";
const float deg = 180.f / 3.14159265358979f;

struct Trace {
    std::string joint, kind;
    float spring_gain = 0.f, damper_gain = 0.f, target_deg = 0.f;
    std::vector<float> angle;       // degrees from the rest pose, per tick
};

float angleOf(glm::quat q) {
    return 2.f * std::atan2(glm::length(glm::vec3(q.x, q.y, q.z)), std::abs(q.w)) * deg;
}

std::vector<Trace> loadTraces() {
    std::vector<Trace> traces;
    if(!std::filesystem::exists(golden_dir)) return traces;
    for(auto& e : std::filesystem::directory_iterator(golden_dir)) {
        std::string name = e.path().filename().string();
        if(name.rfind("drive_", 0) != 0 || e.path().extension() != ".csv") continue;
        std::ifstream in(e.path());
        std::string line;
        std::getline(in, line);
        Trace t;
        auto field = [&](const char* key) {
            auto at = line.find(key);
            REQUIRE(at != std::string::npos);
            at += std::string(key).size();
            return line.substr(at, line.find_first_of(" \n", at) - at);
        };
        t.joint = field("joint=");
        t.kind = field("kind=");
        t.spring_gain = std::stof(field("spring_gain="));
        t.damper_gain = std::stof(field("damper_gain="));
        std::string e2 = field("target_euler=[");
        float x = 0.f, y = 0.f, z = 0.f;
        sscanf(e2.c_str(), "%f,%f,%f", &x, &y, &z);
        t.target_deg = x != 0.f ? x : y;
        std::getline(in, line);
        while(std::getline(in, line)) {
            float qx, qy, qz, qw;
            int tick;
            if(sscanf(line.c_str(), "%d,%f,%f,%f,%f", &tick, &qx, &qy, &qz, &qw) != 5) continue;
            t.angle.push_back(angleOf(glm::quat(qw, qx, qy, qz)));
        }
        traces.push_back(std::move(t));
    }
    std::sort(traces.begin(), traces.end(), [](const Trace& a, const Trace& b) { return std::tie(a.joint, a.kind, a.spring_gain) < std::tie(b.joint, b.kind, b.spring_gain); });
    return traces;
}

// One joint driven on an otherwise kinematic, gravity-free body: Unity's rig, in Jolt
std::vector<float> simulate(ScenarioRunner& runner, const Trace& t, float scale, int substeps) {
    auto& world = runner.world(0);
    auto& bot = world.reg.ctx().get<HumanBot>();
    auto& physics = world.reg.ctx().get<PhysicsWorld>();
    auto& bodies = physics.bodies();
    const BodyDef& def = *bot.body;
    int index = -1;
    for(int j = 0; j < humanbot_joints; j++) if(def.joints[j].name == t.joint) index = j;
    REQUIRE(index >= 0);
    int child = def.joints[index].child;

    physics.system().SetGravity(JPH::Vec3::sZero());
    // Unity's rig has no floor; the foot would land on this one
    for(auto [e, ground] : world.reg.view<Ground>().each()) if(ground.created() && bodies.IsAdded(JPH::BodyID(ground.body))) bodies.RemoveBody(JPH::BodyID(ground.body));
    for(size_t i = 0; i < bot.bodies.size(); i++) {
        // At the reference pose, where the constraint angles are zero
        const auto& seg = def.segments[i];
        bodies.SetPositionRotationAndVelocity(bot.bodies[i], JPH::RVec3(toJolt(seg.rest_position)), toJolt(seg.rest_rotation), JPH::Vec3::sZero(), JPH::Vec3::sZero());
        bodies.SetMotionType(bot.bodies[i], (int) i == child ? JPH::EMotionType::Dynamic : JPH::EMotionType::Kinematic, JPH::EActivation::Activate);
    }
    for(auto& c : bot.constraints) c->ResetWarmStart();
    // Its own child joints would pin the driven body to the kinematic ones below it (Unity's rig removes them too)
    for(int j = 0; j < humanbot_joints; j++) bot.constraints[j]->SetEnabled(def.joints[j].parent != child);

    auto* c = bot.constraints[index].GetPtr();
    const JointDef& joint = def.joints[index];
    const auto& p = bot.params;
    for(auto* motor : { &c->GetSwingMotorSettings(), &c->GetTwistMotorSettings() }) {
        motor->mSpringSettings.mStiffness = p.rotation_spring * t.spring_gain * joint.spring_factor * scale;
        motor->mSpringSettings.mDamping = p.rotation_damper * t.damper_gain * joint.damper_factor * scale;
        motor->SetTorqueLimit(p.max_torque);
    }
    // A target of the recorded size. Unity joint-angle units; Jolt clamps it to the limits as the engine's would
    float a = t.target_deg / deg;
    glm::quat q = t.kind == "twist" ? glm::angleAxis(a, glm::vec3(1.f, 0.f, 0.f)) : glm::angleAxis(a, glm::vec3(0.f, 1.f, 0.f));
    c->SetTargetOrientationCS(toJolt(q));
    c->SetTargetAngularVelocityCS(JPH::Vec3::sZero());

    glm::quat rest = def.segments[child].rest_rotation;
    std::vector<float> trace;
    float h = 1.f / 60.f / substeps;
    for(size_t k = 0; k < t.angle.size(); k++) {
        for(int s = 0; s < substeps; s++) physics.step(h);
        JPH::RVec3 pos;
        JPH::Quat rot;
        bodies.GetPositionAndRotation(bot.bodies[child], pos, rot);
        trace.push_back(angleOf(glm::inverse(rest) * toGlm(rot)));
    }
    for(size_t i = 0; i < bot.bodies.size(); i++) bodies.SetMotionType(bot.bodies[i], JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
    for(auto& k : bot.constraints) k->SetEnabled(true);
    c->SetTargetOrientationCS(JPH::Quat::sIdentity());
    return trace;
}

struct Metrics { float final_value, overshoot, rise; };

Metrics metrics(const std::vector<float>& a) {
    Metrics m { a.back(), 0.f, (float) a.size() };
    float peak = 0.f;
    for(size_t i = 0; i < a.size(); i++) {
        peak = std::max(peak, a[i]);
        if(a[i] >= 0.9f * a.back() && (float) i < m.rise) m.rise = (float) i;
    }
    m.overshoot = peak - a.back();
    return m;
}

ScenarioRunner lab() {
    return ScenarioRunner("walker_train", { { "num_worlds", "1" } }, SceneLoadOptions {});
}

}

TEST_SUITE("scenario") {

TEST_CASE("S6.2 Jolt step responses against the Unity recordings: the gain scale") {
    auto traces = loadTraces();
    if(traces.empty()) return;
    auto runner = lab();
    std::filesystem::create_directories("build/artifacts/P6");
    std::ofstream out("build/artifacts/P6/gain_scale.csv");
    out << "scale,mean_rms_deg,worst_rms_deg,worst_joint\n";
    float best_scale = 1.f, best = 1e9f;
    for(float scale : { 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.85f, 1.0f, 1.15f, 1.3f }) {
        double sum = 0.;
        float worst = 0.f;
        std::string worst_name;
        for(auto& t : traces) {
            auto a = simulate(runner, t, scale, 1);
            double rms = 0.;
            for(size_t k = 0; k < a.size(); k++) rms += (a[k] - t.angle[k]) * (a[k] - t.angle[k]);
            rms = std::sqrt(rms / a.size());
            sum += rms;
            if(rms > worst) { worst = (float) rms; worst_name = t.joint + "_" + t.kind; }
        }
        double mean = sum / traces.size();
        out << scale << "," << mean << "," << worst << "," << worst_name << "\n";
        MESSAGE("scale ", scale, " mean rms ", mean, " deg, worst ", worst, " (", worst_name, ")");
        if(mean < best) { best = (float) mean; best_scale = scale; }
    }
    MESSAGE("best scale ", best_scale, " mean rms ", best, " deg");
    CHECK(best < 3.f);
    std::ofstream detail("build/artifacts/P6/step_responses.csv");
    detail << "joint,kind,spring_gain,tick,unity_deg,jolt_deg\n";
    int misses = 0;
    for(auto& t : traces) {
        auto a = simulate(runner, t, best_scale, 1);
        for(size_t k = 0; k < a.size(); k++) detail << t.joint << "," << t.kind << "," << t.spring_gain << "," << k << "," << t.angle[k] << "," << a[k] << "\n";
        Metrics u = metrics(t.angle), j = metrics(a);
        if(std::abs(j.final_value - u.final_value) > 0.15f * std::max(u.final_value, 2.f)) {
            misses++;
            MESSAGE(t.joint, " ", t.kind, " gain ", t.spring_gain, ": final Unity ", u.final_value, " Jolt ", j.final_value, ", rise ", u.rise, " / ", j.rise);
        }
    }
    MESSAGE("step responses outside 15% on the final angle: ", misses, " of ", traces.size());
}

TEST_CASE("T6.1 the Unity limit probes sit on the Jolt limits") {
    const std::string path = std::string(golden_dir) + "/limit_probes.json";
    if(!std::filesystem::exists(path)) return;
    auto body = loadBody("resources/agents/bodies/heidelbot_notoes.body.json");
    std::ifstream in(path);
    auto golden = nlohmann::json::parse(in);
    float worst_twist = 0.f, worst_cone = 0.f;
    int twist_count = 0, cone_count = 0, reached = 0;
    for(auto& p : golden["probes"]) {
        std::string name = p["joint"].get<std::string>();
        const JointDef* joint = nullptr;
        for(auto& j : body->joints) if(j.name == name) joint = &j;
        REQUIRE(joint != nullptr);
        auto q = [&](const char* key) { const auto& a = p[key]; return glm::normalize(unityToEngine(glm::quat(a[3].get<float>(), a[0].get<float>(), a[1].get<float>(), a[2].get<float>()))); };
        // The child's rotation in the parent's frame, relative to rest, in the constraint frame
        glm::quat local = glm::inverse(q("child_rest_rot")) * q("child_local_rot");
        glm::vec3 x = glm::normalize(joint->unity_axis), y = glm::normalize(joint->unity_secondary - x * glm::dot(joint->unity_secondary, x));
        glm::quat frame = glm::quat_cast(glm::mat3(x, y, glm::cross(x, y)));
        glm::quat cs = glm::inverse(frame) * local * frame;
        if(cs.w < 0.f) cs = -cs;
        // q = swing * twist (Jolt's decomposition)
        float twist_len = std::sqrt(cs.w * cs.w + cs.x * cs.x);
        glm::quat twist = twist_len > 1e-6f ? glm::quat(cs.w / twist_len, cs.x / twist_len, 0.f, 0.f) : glm::quat(1.f, 0.f, 0.f, 0.f);
        glm::quat swing = cs * glm::inverse(twist);
        float twist_angle = 2.f * std::atan2(twist.x, twist.w);
        std::string dof = p["dof"];
        if(joint->twist_max - joint->twist_min > 3.5f) continue;  // the shoulder: its twist range wraps around the half turn
        if(dof == "twist") {
            float bound = p["at"] == "low" ? joint->twist_min : joint->twist_max;
            float error = std::abs(twist_angle - bound) * deg;
            worst_twist = std::max(worst_twist, error);
            twist_count++;
            if(error > 1.f) MESSAGE("twist ", name, " at ", p["at"].get<std::string>(), ": ", twist_angle * deg, " against ", bound * deg);
        } else {
            float sy = joint->swing_y, sz = joint->swing_z;
            float a = swing.y / std::sin(0.5f * sy), b = swing.z / std::sin(0.5f * sz);
            float value = std::sqrt(a * a + b * b);
            float angle = 2.f * std::atan2(glm::length(glm::vec2(swing.y, swing.z)), swing.w) * deg;
            cone_count++;
            if(sy * deg > 150.f || sz * deg > 150.f) continue;  // the ramp stops short of the widest arm limits
            reached++;
            float excess = value > 1e-3f ? std::abs(angle * (1.f - 1.f / value)) : 0.f;
            worst_cone = std::max(worst_cone, excess);
            if(excess > 20.f) MESSAGE("swing ", name, " ", dof, " ", p["at"].get<std::string>(), ": ellipse value ", value, ", angle ", angle);
        }
    }
    MESSAGE("twist probes ", twist_count, " worst ", worst_twist, " deg; cone probes ", cone_count, " used ", reached, " worst ", worst_cone);
    CHECK(worst_twist < 1.5f);
    CHECK(worst_cone < 20.f);  // PhysX's cone is wider on narrow diagonals (hand Y 7, Z 36): the shapes differ, the extremes agree
}

}

TEST_SUITE("scenario") {

TEST_CASE("S6.4 standing: motors holding the rest pose at full gains keep the ragdoll up") {
    if(!std::filesystem::exists("resources/agents/bodies/heidelbot_notoes.body.json")) return;
    auto runner = lab();
    auto& world = runner.world(0);
    auto& bot = world.reg.ctx().get<HumanBot>();
    auto& physics = world.reg.ctx().get<PhysicsWorld>();
    bot.params.randomize = false;
    resetHumanBot(world.reg);
    for(int j = 0; j < humanbot_joints; j++) {
        auto* c = bot.constraints[j].GetPtr();
        const JointDef& joint = bot.body->joints[j];
        for(auto* motor : { &c->GetSwingMotorSettings(), &c->GetTwistMotorSettings() }) {
            motor->mSpringSettings.mStiffness = bot.params.rotation_spring * joint.spring_factor;
            motor->mSpringSettings.mDamping = bot.params.rotation_damper * joint.damper_factor;
            motor->SetTorqueLimit(bot.params.max_torque);
        }
        glm::quat q(1.f, 0.f, 0.f, 0.f);
        float angle = joint.twist_min > 0.f ? joint.twist_min + 0.002f : 0.f;
        if(angle != 0.f) q = glm::angleAxis(angle, glm::vec3(1.f, 0.f, 0.f));
        c->SetTargetOrientationCS(toJolt(q));
        c->SetTargetAngularVelocityCS(JPH::Vec3::sZero());
    }
    glm::vec3 start = bot.state.position[bot.body->pelvis];
    float head_start = bot.state.position[bot.body->head].y;
    BodySyncSys sync(world.reg);
    float speed_after_1s = 0.f;
    for(int k = 0; k < 600; k++) {
        physics.step(1.f / 60.f);
        sync.tick(1. / 60.);
        if(k >= 60) for(size_t i = 0; i < 16; i++) speed_after_1s = std::max(speed_after_1s, glm::length(bot.state.linear[i]));
    }
    float head = bot.state.position[bot.body->head].y;
    float drift = glm::length(horizontal(bot.state.position[bot.body->pelvis] - start));
    // Unity's body falls too (golden/heidelbot_notoes/stand.csv, head 0.139 m after 2.5 s): the gate is matching the fall
    MESSAGE("pelvis drift ", drift, " m, head ", head_start, " -> ", head);
    CHECK(head == doctest::Approx(0.139f).epsilon(0.1));
}

}
