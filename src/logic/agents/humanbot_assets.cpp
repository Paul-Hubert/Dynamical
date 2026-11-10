#include "humanbot_assets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace dy;
using json = nlohmann::json;

namespace {

const float deg = 3.14159265358979f / 180.f;

glm::vec3 vec3From(const json& j) { return glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()); }
glm::quat quatFrom(const json& j) { return glm::quat(j.at(3).get<float>(), j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()); }

// Unity names the instance "HeidelBot(Clone)" and keeps a "mixamorig:" prefix on bones
std::string cleanName(std::string name) {
    if(auto p = name.find("(Clone)"); p != std::string::npos) name.erase(p);
    if(auto p = name.rfind(':'); p != std::string::npos) name = name.substr(p + 1);
    return name;
}

// The smallest rotation taking a to b, both unit
glm::quat between(glm::vec3 a, glm::vec3 b) {
    float d = glm::dot(a, b);
    if(d < -0.999999f) {
        glm::vec3 axis = std::abs(a.x) < 0.9f ? glm::normalize(glm::cross(a, glm::vec3(1.f, 0.f, 0.f))) : glm::normalize(glm::cross(a, glm::vec3(0.f, 1.f, 0.f)));
        return glm::angleAxis(3.14159265358979f, axis);
    }
    glm::vec3 c = glm::cross(a, b);
    return glm::normalize(glm::quat(1.f + d, c.x, c.y, c.z));
}

const json& require(const json& j, const char* key, const std::string& where) {
    auto it = j.find(key);
    if(it == j.end()) throw AssetError(where + ": missing " + key);
    return *it;
}

void checkFormat(const json& j, const std::string& where) {
    if(j.value("format_version", 0) != 1) throw AssetError(where + ": unknown format_version");
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if(!in) throw AssetError(path + ": cannot open");
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

}

int BodyDef::segmentIndex(const std::string& n) const {
    for(int i = 0; i < (int) segments.size(); i++) if(segments[i].name == n) return i;
    return -1;
}

int BodyDef::frameSegment(int j) const {
    int p = joints[j].parent;
    return p == pelvis ? spine : p;
}

float BodyDef::est0() const {
    float head_y = segments[head].spawn_position.y;
    return std::max({ head_y - segments[left_foot].spawn_position.y, head_y - segments[right_foot].spawn_position.y, 0.f });
}

std::shared_ptr<const BodyDef> dy::parseBody(const json& j, const std::string& hash) {

    auto out = std::make_shared<BodyDef>();
    BodyDef& b = *out;
    b.name = j.value("name", "");
    const std::string where = "body " + b.name;
    checkFormat(j, where);
    if(j.value("frame", "") != "unity_left_handed") throw AssetError(where + ": expected frame unity_left_handed");
    b.file_hash = hash;
    b.total_mass = j.value("total_mass", 70.f);
    b.height = j.value("height", 1.89f);
    b.unity_est0 = j.value("est0", 0.f);
    b.unity_lowest_y = j.value("lowest_shape_y", 0.f);
    b.linear_damping = j.value("linear_damping", 0.f);
    b.angular_damping = j.value("angular_damping", 0.f);
    if(auto m = j.find("materials"); m != j.end() && m->contains("heidelbot")) {
        b.friction = (*m)["heidelbot"].value("friction", 1.f);
        b.restitution = (*m)["heidelbot"].value("bounciness", 0.f);
    }

    double percent = 0.;
    for(auto& s : require(j, "segments", where)) {
        SegmentDef seg;
        seg.name = cleanName(s.value("name", ""));
        seg.role = s.value("role", "");
        seg.rest_position = unityToEngine(vec3From(s.at("rest_world_pos")));
        seg.rest_rotation = unityToEngine(quatFrom(s.at("rest_world_rot")));
        if(std::abs(glm::length(seg.rest_rotation) - 1.f) > 1e-3f) throw AssetError(where + ": segment " + seg.name + " has a non-unit rotation");
        seg.rest_rotation = glm::normalize(seg.rest_rotation);
        seg.percentage = s.at("body_percentage").get<float>();
        percent += seg.percentage;
        glm::quat inverse = glm::inverse(seg.rest_rotation);
        seg.center_of_mass = inverse * (unityToEngine(vec3From(s.at("world_center_of_mass"))) - seg.rest_position);
        seg.inertia = vec3From(s.at("inertia_tensor"));
        if(seg.inertia.x <= 0.f || seg.inertia.y <= 0.f || seg.inertia.z <= 0.f) throw AssetError(where + ": segment " + seg.name + " has no inertia");
        seg.inertia_rotation = glm::normalize(unityToEngine(quatFrom(s.at("inertia_tensor_rotation"))));
        for(auto& sh : s.at("shapes")) {
            ShapeDef shape;
            shape.source = sh.value("source", "");
            glm::vec3 center = unityToEngine(vec3From(sh.at("center")));
            glm::quat rotation = glm::normalize(unityToEngine(quatFrom(sh.at("rotation"))));
            shape.center = inverse * (center - seg.rest_position);
            std::string type = sh.value("type", "");
            if(type == "capsule") {
                shape.type = ShapeDef::Type::Capsule;
                shape.radius = sh.at("radius").get<float>();
                float height = sh.at("height").get<float>();
                shape.half_height = std::max(height * 0.5f - shape.radius, 0.f);
                int direction = sh.value("direction", 1);
                glm::vec3 axis(direction == 0, direction == 1, direction == 2);
                // Mirrored rotation times the mirrored axis; the sign of an axis does not matter for a capsule
                glm::vec3 world = rotation * unityToEngine(axis);
                shape.rotation = between(glm::vec3(0.f, 1.f, 0.f), glm::normalize(inverse * world));
            } else if(type == "box") {
                shape.type = ShapeDef::Type::Box;
                shape.half_extents = vec3From(sh.at("size")) * 0.5f;
                shape.rotation = glm::normalize(inverse * rotation);
            } else if(type == "sphere") {
                shape.type = ShapeDef::Type::Sphere;
                shape.radius = sh.at("radius").get<float>();
                shape.rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
            } else throw AssetError(where + ": unknown shape type " + type);
            seg.shapes.push_back(shape);
        }
        if(seg.shapes.empty()) throw AssetError(where + ": segment " + seg.name + " has no shape");
        b.segments.push_back(std::move(seg));
    }
    if(std::abs(percent - 99.98) > 0.05) throw AssetError(where + ": the body percentages sum to " + std::to_string(percent));

    // Parents come after every segment is known; they must precede their children
    int si = 0;
    for(auto& s : j.at("segments")) {
        std::string parent = cleanName(s.value("parent", ""));
        int p = parent.empty() ? -1 : b.segmentIndex(parent);
        if(!parent.empty() && p < 0) throw AssetError(where + ": unknown parent " + parent);
        if(p >= si) throw AssetError(where + ": parent after child at " + b.segments[si].name);
        b.segments[si].parent = p;
        const std::string role = s.value("role", "");
        if(role == "pelvis") b.pelvis = si;
        if(role == "spine") b.spine = si;
        if(role == "head") b.head = si;
        if(role == "foot") (b.segments[si].name.find("Left") != std::string::npos ? b.left_foot : b.right_foot) = si;
        si++;
    }
    if(b.pelvis < 0 || b.spine < 0 || b.head < 0 || b.left_foot < 0 || b.right_foot < 0) throw AssetError(where + ": a role (pelvis, spine, head, feet) is missing");

    for(auto& jd : require(j, "joints", where)) {
        JointDef joint;
        joint.name = cleanName(jd.value("name", ""));
        joint.parent = b.segmentIndex(cleanName(jd.value("parent", "")));
        joint.child = b.segmentIndex(cleanName(jd.value("child", "")));
        if(joint.parent < 0 || joint.child < 0) throw AssetError(where + ": joint " + joint.name + " names an unknown segment");
        if(b.segments[joint.child].parent != joint.parent) throw AssetError(where + ": joint " + joint.name + " does not join a child to its parent");
        joint.side = jd.value("side", std::string("C"))[0];
        glm::vec3 anchor = unityToEngine(vec3From(jd.at("anchor_world")));
        glm::vec3 connected = unityToEngine(vec3From(jd.at("connected_anchor_world")));
        if(glm::length(anchor - connected) > 1e-3f) throw AssetError(where + ": joint " + joint.name + " anchors do not coincide");
        joint.anchor = anchor;
        joint.unity_axis = glm::normalize(unityToEngine(vec3From(jd.at("axis"))));
        joint.unity_secondary = glm::normalize(unityToEngine(vec3From(jd.at("secondary_axis"))));
        const glm::quat& child = b.segments[joint.child].rest_rotation;
        joint.twist_axis = glm::normalize(child * joint.unity_axis);
        joint.plane_axis = glm::normalize(child * joint.unity_secondary);
        float lo = jd.at("low_x_deg").get<float>() * deg, hi = jd.at("high_x_deg").get<float>() * deg;
        joint.twist_min = lo;
        joint.twist_max = hi;
        const float max_swing = 179.f * deg;
        joint.swing_y = std::min(jd.at("y_deg").get<float>() * deg, max_swing);
        joint.swing_z = std::min(jd.at("z_deg").get<float>() * deg, max_swing);
        joint.spring_factor = jd.value("spring_factor", 1.f);
        joint.damper_factor = jd.value("damper_factor", 1.f);
        joint.torque_factor = jd.value("torque_factor", 1.f);
        if(joint.name == "Spine") b.spine_joint = (int) b.joints.size();
        b.joints.push_back(joint);
    }
    if(b.joints.size() != 15 || b.segments.size() != 16) throw AssetError(where + ": expected 16 segments and 15 joints");
    if(b.spine_joint < 0) throw AssetError(where + ": no Spine joint");

    // Spawn pose: the rest pose, each joint's zero moved inside its limits (the knee's low stop is 2 degrees while
    // the rest pose is straight). The constraints keep the rest pose as their reference
    for(auto& s : b.segments) {
        s.spawn_position = s.rest_position;
        s.spawn_rotation = s.rest_rotation;
    }
    const float margin = 0.1f * deg;
    for(size_t ji = 0; ji < b.joints.size(); ji++) {
        const JointDef& joint = b.joints[ji];
        float angle = 0.f;
        if(joint.twist_min > 0.f) angle = joint.twist_min + margin;
        else if(joint.twist_max < 0.f) angle = joint.twist_max - margin;
        if(angle == 0.f) continue;
        glm::vec3 pivot = b.segments[joint.child].spawn_position;
        glm::vec3 axis = b.segments[joint.child].spawn_rotation * joint.unity_axis;
        glm::quat turn = glm::angleAxis(angle, glm::normalize(axis));
        // Segments are parents first: a segment is moved when its chain reaches this joint's child
        std::vector<char> moved(b.segments.size(), 0);
        moved[joint.child] = 1;
        for(int i = 0; i < (int) b.segments.size(); i++) {
            if(i != joint.child && (b.segments[i].parent < 0 || !moved[b.segments[i].parent])) continue;
            moved[i] = 1;
            auto& s = b.segments[i];
            s.spawn_position = pivot + turn * (s.spawn_position - pivot);
            s.spawn_rotation = glm::normalize(turn * s.spawn_rotation);
        }
    }

    return out;

}

std::shared_ptr<const BodyDef> dy::loadBody(const std::string& path) {
    json j;
    try {
        j = json::parse(readFile(path));
    } catch(const json::exception& e) {
        throw AssetError(path + ": " + e.what());
    }
    return parseBody(j, fileSha256(path));
}

glm::vec3 PoseTrack::position(int frame, int segment) const {
    const float* p = data.data() + (size_t) frame * segments * 7 + segment * 7;
    return glm::vec3(p[0], p[1], p[2]);
}

glm::quat PoseTrack::rotation(int frame, int segment) const {
    const float* p = data.data() + (size_t) frame * segments * 7 + segment * 7;
    return glm::quat(p[6], p[3], p[4], p[5]);
}

std::shared_ptr<const PoseTrack> dy::loadTrack(const std::string& path) {

    json j = json::parse(readFile(path));
    checkFormat(j, path);
    auto out = std::make_shared<PoseTrack>();
    out->body = j.value("body", "");
    out->controller = j.value("controller", "");
    out->dt = j.value("dt", 1.f / 60.f);
    out->state_speed = j.value("state_speed", 1.f);
    out->frames = j.value("frames", 0);
    out->segments = j.value("segments", 0);

    std::filesystem::path bin(path);
    bin.replace_extension("");
    bin.replace_extension(".track.bin");
    std::string bytes = readFile(bin.string());
    size_t expected = (size_t) out->frames * out->segments * 7;
    if(bytes.size() != expected * 4) throw AssetError(bin.string() + ": wrong size");
    out->data.resize(expected);
    std::memcpy(out->data.data(), bytes.data(), bytes.size());
    out->file_hash = fileSha256(path) + fileSha256(bin.string());

    // To the engine frame, once (replays written by the engine already are)
    if(j.value("frame", "") != "engine") for(size_t i = 0; i < expected; i += 7) {
        float* p = out->data.data() + i;
        p[2] = -p[2];
        p[3] = -p[3];
        p[4] = -p[4];
    }
    return out;

}

AgentParams dy::parseParams(const json& j) {

    AgentParams p;
    checkFormat(j, "params");
    p.behavior = j.value("behavior", p.behavior);
    p.body = j.value("body", "");
    const json& u = j.at("unity_parameters").at("MonoBehaviour");
    const json& e = j.at("unity_energy").at("MonoBehaviour");

    auto f = [&](const char* key, float& out) { if(u.contains(key)) out = u[key].get<float>(); };
    auto i = [&](const char* key, int& out) { if(u.contains(key)) out = u[key].get<int>(); };
    auto b = [&](const char* key, bool& out) { if(u.contains(key)) out = u[key].is_boolean() ? u[key].get<bool>() : u[key].get<int>() != 0; };
    auto ef = [&](const char* key, float& out) { if(e.contains(key)) out = e[key].get<float>(); };

    i("maxFrames", p.max_frames_60hz);
    i("arcFrames", p.arc_frames_60hz);
    f("totalMass", p.total_mass);
    f("rotationSpring", p.rotation_spring);
    f("rotationDamper", p.rotation_damper);
    f("maxTorque", p.max_torque);
    f("torqueFactor", p.torque_factor);
    f("maxAngularVelocity", p.max_angular_velocity);
    f("maxTargetAngularVelocity", p.max_target_angular_velocity);
    f("targetSpeed", p.target_speed);
    if(u.contains("speeds")) p.speeds = u["speeds"].get<std::vector<float>>();
    b("continuousSpeed", p.continuous_speed);
    b("randomizing", p.randomize);
    b("randomTarget", p.random_target);
    b("randomSpeed", p.random_speed);
    b("randomInit", p.random_init);
    f("randomInitChance", p.random_init_chance);
    b("pushing", p.pushing);
    b("pulling", p.pulling);
    f("pushChance", p.push_chance);
    f("pushForce", p.push_force);
    f("pullChance", p.pull_chance);
    f("pullForce", p.pull_force);
    f("rootReward", p.root_reward);
    f("headSpringReward", p.head_spring_reward);
    f("headDamperReward", p.head_damper_reward);
    f("lifeReward", p.life_reward);
    f("dyingReward", p.dying_reward);
    f("winReward", p.win_reward);
    f("deathPenalty", p.death_penalty);
    f("rewardFactor", p.reward_factor);
    f("collisionPenalty", p.collision_penalty);
    f("dyingHeightFactor", p.dying_height_factor);
    i("maxDyingFrames", p.max_dying_frames);
    b("dieOnFall", p.die_on_fall);
    b("resetOnAlive", p.reset_on_alive);
    b("speedDiffZeroRecording", p.speed_diff_zero_recording);

    ef("energyBonusWeight", p.energy_weight);
    ef("energyScale", p.energy_scale);
    ef("speedEdgeWidth", p.energy_speed_edge);
    ef("minSpeedFloor", p.energy_min_speed_floor);
    ef("velocitySmoothingTau", p.energy_tau);
    ef("positiveEnergyMultiplier", p.energy_positive_multiplier);
    ef("negativeEnergyMultiplier", p.energy_negative_multiplier);
    ef("torqueRateWeight", p.torque_rate_weight);
    ef("statsSettleSeconds", p.stats_settle_seconds);

    for(const char* flag : { "springControl", "angularVelocityControl", "jointSpace" }) {
        bool on = true;
        b(flag, on);
        if(!on) throw AssetError(std::string("params: layout heidelbot_notoes needs ") + flag);
    }
    return p;

}

std::shared_ptr<const AgentParams> dy::loadParams(const std::string& path) {
    // EditorJsonUtility writes break forces as the bare token Infinity, which is not JSON
    std::string text = readFile(path);
    for(size_t at = 0; (at = text.find("Infinity", at)) != std::string::npos;) text.replace(at, 8, "1e300");
    json j = json::parse(text);
    auto p = std::make_shared<AgentParams>(parseParams(j));
    p->file_hash = fileSha256(path);
    return p;
}

// ---- SHA-256

namespace {

constexpr std::array<uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

}

std::string dy::fileSha256(const std::string& path) {

    std::string bytes = readFile(path);
    uint64_t bits = (uint64_t) bytes.size() * 8;
    bytes.push_back((char) 0x80);
    while(bytes.size() % 64 != 56) bytes.push_back(0);
    for(int i = 7; i >= 0; i--) bytes.push_back((char) ((bits >> (i * 8)) & 0xff));

    std::array<uint32_t, 8> h = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    for(size_t chunk = 0; chunk < bytes.size(); chunk += 64) {
        std::array<uint32_t, 64> w {};
        for(int i = 0; i < 16; i++) {
            const auto* p = reinterpret_cast<const unsigned char*>(bytes.data()) + chunk + i * 4;
            w[i] = (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
        }
        for(int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto v = h;
        for(int i = 0; i < 64; i++) {
            uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
            uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            uint32_t t1 = v[7] + s1 + ch + K[i] + w[i];
            uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
            uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            uint32_t t2 = s0 + maj;
            v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
            v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
        }
        for(int i = 0; i < 8; i++) h[i] += v[i];
    }
    static const char* hex = "0123456789abcdef";
    std::string out;
    for(uint32_t x : h) for(int i = 7; i >= 0; i--) out.push_back(hex[(x >> (i * 4)) & 15]);
    return out;

}
