#include "animal_agent.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>

#include "logic/agents/agent_env_params.h"
#include "logic/agents/agent_io.h"
#include "logic/agents/ground_field.h"
#include "logic/agents/humanbot.h"
#include "logic/components/model_ref.h"
#include "logic/components/transform.h"
#include "physics/physics_sys.h"
#include "physics/physics_world.h"
#include "physics/terrain_colliders.h"
#include "scene/component_registry.h"
#include "scene/scenario_runner.h"
#include "scene/system_registry.h"
#include "scene/world.h"
#include "util/log.h"
#include "util/paths.h"

using namespace dy;

namespace {

constexpr float pi = 3.14159265358979f;

float wrapAngle(float a) {
    while(a > pi) a -= 2.f * pi;
    while(a < -pi) a += 2.f * pi;
    return a;
}

void applyEnvParams(entt::registry& reg, AnimalBot& bot) {
    AnimalParams p = *bot.base_params;
    if(auto* env = reg.ctx().find<AgentEnvParams>()) {
        auto f = [&](const char* name, float& v) { v = env->get(name, v); };
        auto b = [&](const char* name, bool& v) { v = env->get(name, v ? 1.f : 0.f) > 0.f; };
        f("rootReward", p.root_reward);
        f("deathPenalty", p.death_penalty);
        f("lifeReward", p.life_reward);
        f("rewardFactor", p.reward_factor);
        f("fallHeight", p.fall_height);
        f("softResistance", p.soft_resistance);
        f("softLift", p.soft_lift);
        f("headReward", p.head_reward);
        f("modeReward", p.mode_reward);
        b("dieOnFall", p.die_on_fall);
        b("randomYaw", p.random_yaw);
    }
    bot.params = p;
}

void setPark(entt::registry& reg, bool value) {
    if(auto* parked = reg.ctx().find<Parked>()) parked->value = value;
}

int episodeTicks(const entt::registry& reg, const AnimalBot& bot) {
    if(bot.view && !bot.view_truncate) return std::numeric_limits<int>::max();
    const auto& clock = reg.ctx().get<WorldClock>();
    return (int) std::lround(bot.params.max_frames_60hz * (1. / clock.dt) / 60.);
}

// The speed of the clip the commands are in: the walk's for a physical animal, the gait's for a recording
float commandSpeed(const AnimalBot& bot, const AnimalWalkState* walk) {
    if(walk && walk->ready) {
        const auto& clip = walk->walk.species->clips[walk->walk.model.clip];
        return clip.speed > 0.02f ? clip.speed : walk->walk.model.speed;
    }
    return bot.body->walk_speed;
}

// A new command for a new episode or walk
void beginCommand(entt::registry& reg, AnimalBot& bot, float yaw, float clip_speed) {
    auto& rng = reg.ctx().get<WorldRng>();
    if(bot.spawn.speed > 0.f) {
        bot.command = WalkCommand();
        bot.command.yaw = yaw;
        bot.command.speed = bot.command.wanted_speed = bot.spawn.speed * clip_speed;
        bot.command.until = std::numeric_limits<float>::max();
        bot.command.started = true;
    } else {
        startCommand(bot.command, bot.ranges, clip_speed, bot.body->length, yaw, rng);
    }
}

// On a terrain the heading is drawn where the ground stays in the slope band, and turned to at the ranges' tightest
// turn; the speed is drawn as everywhere
void advanceCommand(entt::registry& reg, AnimalBot& bot, glm::vec3 from, float clip_speed, float dt) {
    auto& rng = reg.ctx().get<WorldRng>();
    if(bot.spawn.speed > 0.f) {
        bot.command.time += dt;
        return;
    }
    const float before = bot.command.until;
    stepCommand(bot.command, bot.ranges, clip_speed, bot.body->length, rng, dt);
    if(!(bot.routes && bot.ground)) return;
    if(bot.command.until != before || !bot.command.has_target) {
        auto drawn = drawDirection(*bot.ground, rng, *bot.routes, glm::vec3(from.x, bot.groundUnder(from), from.z));
        if(!drawn) {
            bot.route_lost = true;
            return;
        }
        bot.command.target_yaw = std::atan2(-drawn->x, -drawn->z);
        bot.command.has_target = true;
    }
    const float tightest = 1.f / std::max(bot.ranges.radius[0] * bot.body->length, 0.1f);
    bot.command.wanted_turn = std::clamp(wrapAngle(bot.command.target_yaw - bot.command.yaw) / std::max(bot.body->length, 0.1f), -tightest, tightest);
}

float piece(float a, float lo, float mid, float hi) {
    return a >= 0.f ? mid + a * (hi - mid) : mid + a * (mid - lo);
}

void findHead(AnimalBot& bot);

// A new order of the single animal's task: the mode, the head offsets that go with it, and how fast it may go then
void drawOrder(AnimalBot& bot, WorldRng& rng) {
    const double u = rng.uniform();
    bot.mode = u < 0.45 ? (int) AnimalMode::Move : u < 0.62 ? (int) AnimalMode::Feed : u < 0.8 ? (int) AnimalMode::Attack
        : u < 0.9 ? (int) AnimalMode::Mate : (int) AnimalMode::Rest;
    glm::vec2 head(0.f);
    switch((AnimalMode) bot.mode) {
    case AnimalMode::Move:
        if(rng.uniform() < 0.5) head = glm::vec2((float) rng.uniform(-0.8, 0.8), (float) rng.uniform(-0.3, 0.3));
        break;
    case AnimalMode::Feed:
        head = glm::vec2((float) rng.uniform(-0.3, 0.3), (float) rng.uniform(-animal_head_down, -0.5));
        break;
    case AnimalMode::Attack:
        head = glm::vec2((float) rng.uniform(-0.6, 0.6), (float) rng.uniform(-0.5, 0.3));
        break;
    default:
        head = glm::vec2((float) rng.uniform(-0.2, 0.2), (float) rng.uniform(-0.2, 0.2));
        break;
    }
    bot.head = clampHead(head);
    bot.order_until = bot.command.time + (float) rng.uniform(2., 6.);
}

// What a mode leaves of the speed the command drew
float orderSpeed(int mode) {
    switch((AnimalMode) mode) {
    case AnimalMode::Feed: return 0.15f;
    case AnimalMode::Mate: case AnimalMode::Rest: return 0.f;
    default: return 1.f;
    }
}

}

const char* dy::animalModeName(int mode) {
    static const char* const names[animal_modes] = { "move", "feed", "attack", "mate", "rest" };
    return mode >= 0 && mode < animal_modes ? names[mode] : "?";
}

glm::vec2 dy::clampHead(glm::vec2 head) {
    return glm::vec2(std::clamp(head.x, -animal_head_yaw, animal_head_yaw), std::clamp(head.y, -animal_head_down, animal_head_up));
}

glm::vec3 AnimalBot::headDirection() const {
    glm::vec3 d = rotation[head_segment] * muzzle;
    float length = glm::length(d);
    return length > 1e-6f ? d / length : rotation[head_segment] * glm::vec3(0.f, 0.f, -1.f);
}

glm::vec3 AnimalBot::askedHeadDirection(glm::vec2 asked) const {
    // In the trunk's heading frame (x forward, y up, z to the right): the standing direction, pitched then turned
    const float elevation = std::asin(std::clamp(head_stand.y, -1.f, 1.f)) + asked.y;
    const float azimuth = std::atan2(-head_stand.z, head_stand.x) + asked.x;
    glm::vec3 local(std::cos(elevation) * std::cos(azimuth), std::sin(elevation), -std::cos(elevation) * std::sin(azimuth));
    HeadingFrame frame = headingFrame(rotation[0]);
    return local.x * frame.x + local.y * frame.y + local.z * frame.z;
}

void dy::observeOrder(const AnimalBot& bot, float* out) {
    out[0] = bot.head.x / animal_head_yaw;
    out[1] = bot.head.y / animal_head_down;
    for(int m = 0; m < animal_modes; m++) out[2 + m] = bot.mode == m ? 1.f : 0.f;
}

AnimalRating dy::rateAnimal(const AnimalBot& bot, double window) {

    AnimalRating r;
    const AnimalParams& p = bot.params;
    const AnimalBody& body = *bot.body;
    const glm::vec3 trunk = bot.position[0];
    r.velocity = (trunk - bot.snapshot_trunk) / (float) window;
    r.along = glm::dot(r.velocity, bot.snapshot_direction);
    r.root = std::min(r.along, bot.snapshot_speed) * p.root_reward;
    if(bot.snapshot_speed > 0.f) r.root /= bot.snapshot_speed;
    HeadingFrame frame = headingFrame(bot.rotation[0]);
    r.heading_error = std::acos(std::clamp(glm::dot(frame.x, bot.snapshot_direction), -1.f, 1.f));

    const glm::vec3 muzzle = bot.muzzlePoint();
    r.muzzle_height = muzzle.y - bot.groundUnder(muzzle);
    if(!bot.orders) return r;

    r.head_error = std::acos(std::clamp(glm::dot(bot.headDirection(), bot.askedHeadDirection(bot.snapshot_head)), -1.f, 1.f));
    r.head = p.head_reward * std::exp(-r.head_error * r.head_error / (2.f * 0.35f * 0.35f));

    const float v0 = std::max(body.walk_speed, 1e-3f);
    float mode = 0.f;
    switch((AnimalMode) bot.snapshot_mode) {
    case AnimalMode::Move:
        break;
    case AnimalMode::Feed:
        // The muzzle down to the ground
        mode = std::exp(-std::max(r.muzzle_height, 0.f) / (0.25f * body.stand_height));
        break;
    case AnimalMode::Attack: {
        // The head thrown where it points
        const glm::vec3 head_velocity = bot.origin_velocity[bot.head_segment];
        mode = std::clamp(glm::dot(head_velocity, bot.headDirection()) / (2.f * v0), 0.f, 1.f);
        break;
    }
    case AnimalMode::Mate:
        mode = std::exp(-glm::length(horizontal(r.velocity)) / (0.3f * v0));
        break;
    case AnimalMode::Rest: {
        float moving = 0.f;
        for(const glm::vec3& v : bot.origin_velocity) moving += glm::length(v);
        mode = std::exp(-moving / (float) bot.origin_velocity.size() / (0.2f * v0));
        break;
    }
    }
    r.mode = p.mode_reward * mode;
    return r;

}

namespace {

// The head: the segment named so, else the one of the trunk's chain that reaches furthest forward. Its muzzle: the
// front of its shape, low, where it stands
void findHead(AnimalBot& bot) {
    const AnimalBody& body = *bot.body;
    const BodyDef& def = body.def;
    int head = -1;
    for(size_t i = 0; i < def.segments.size(); i++) {
        std::string name = def.segments[i].name;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char) std::tolower(c); });
        if(name.find("head") != std::string::npos) head = (int) i;
    }
    if(head < 0) {
        float front = 1e9f;
        for(size_t i = 0; i < def.segments.size(); i++) {
            if(body.may_touch[i] || def.segments[i].role == "leg") continue;
            if(def.segments[i].spawn_position.z < front) {
                front = def.segments[i].spawn_position.z;
                head = (int) i;
            }
        }
    }
    bot.head_segment = std::max(head, 0);

    const SegmentDef& segment = def.segments[bot.head_segment];
    const ShapeDef& shape = segment.shapes[0];
    // The support point of the shape forward and a little down, at the standing pose (the animal looks down -z)
    const glm::vec3 world(0.f, -0.45f, -0.9f);
    const glm::quat to_shape = glm::inverse(segment.spawn_rotation * shape.rotation);
    const glm::vec3 d = glm::normalize(to_shape * world);
    glm::vec3 support(0.f);
    if(shape.type == ShapeDef::Type::Box) {
        // A corner only along the axes the direction really has: the middle of a face or of an edge otherwise
        for(int a = 0; a < 3; a++) support[a] = std::abs(d[a]) > 0.35f ? (d[a] > 0.f ? shape.half_extents[a] : -shape.half_extents[a]) : 0.f;
    } else {
        if(shape.type == ShapeDef::Type::Capsule) support.y = d.y > 0.f ? shape.half_height : -shape.half_height;
        support += d * shape.radius;
    }
    bot.muzzle = shape.center + shape.rotation * support;

    // Where the head points standing, in the trunk's heading frame
    HeadingFrame frame = headingFrame(def.segments[0].spawn_rotation);
    glm::vec3 direction = segment.spawn_rotation * bot.muzzle;
    float length = glm::length(direction);
    bot.head_stand = length > 1e-6f ? frame.project(direction / length) : glm::vec3(1.f, 0.f, 0.f);
}

}

void dy::readAnimalState(entt::registry& reg, AnimalBot& bot) {
    auto& bodies = physicsWorld(reg).bodies();
    for(size_t i = 0; i < bot.parts.bodies.size(); i++) {
        JPH::RVec3 p;
        JPH::Quat q;
        bodies.GetPositionAndRotation(bot.parts.bodies[i], p, q);
        bot.position[i] = toGlm(JPH::Vec3(p));
        bot.rotation[i] = toGlm(q);
        bot.linear[i] = toGlm(bodies.GetLinearVelocity(bot.parts.bodies[i]));
        bot.angular[i] = toGlm(bodies.GetAngularVelocity(bot.parts.bodies[i]));
    }
}

void dy::buildAnimalRagdoll(entt::registry& reg, AnimalBot& bot, const AnimalBuild& options) {
    const AnimalBody& body = *bot.body;
    const BodyDef& def = body.def;
    RagdollBuild build;
    build.total_mass = body.mass;
    build.max_angular_velocity = body.max_angular_velocity;
    build.kinematic = bot.kinematic;
    build.cast = reg.ctx().contains<std::shared_ptr<TerrainColliders>>();
    build.group = options.group;
    build.animal_layer = options.collide;
    buildRagdollParts(physicsWorld(reg), def, build, bot.parts);

    const size_t n = def.segments.size();
    bot.position.assign(n, glm::vec3(0.f));
    bot.linear.assign(n, glm::vec3(0.f));
    bot.angular.assign(n, glm::vec3(0.f));
    bot.rotation.assign(n, glm::quat(1.f, 0.f, 0.f, 0.f));
    bot.origin_previous.assign(n, glm::vec3(0.f));
    bot.origin_velocity.assign(n, glm::vec3(0.f));
    bot.frame_size = animal_header + 9 * (int) n;
    bot.previous_frame.assign(bot.frame_size, 0.f);
    bot.built = true;
    readAnimalState(reg, bot);
    findHead(bot);
}

void dy::destroyAnimalRagdoll(entt::registry& reg, AnimalBot& bot) {
    if(!bot.built) return;
    destroyRagdollParts(physicsWorld(reg), bot.parts);
    bot.built = false;
}

void dy::standAnimalBot(entt::registry& reg, AnimalBot& bot, glm::vec3 origin, float yaw, bool on_field) {
    const AnimalBody& body = *bot.body;
    const BodyDef& def = body.def;
    auto& bodies = physicsWorld(reg).bodies();
    const glm::quat turn = glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f));
    const glm::vec3 pivot(def.segments[0].spawn_position.x, 0.f, def.segments[0].spawn_position.z);
    std::vector<glm::vec3> positions(def.segments.size());
    std::vector<glm::quat> rotations(def.segments.size());
    for(size_t i = 0; i < def.segments.size(); i++) {
        positions[i] = origin + turn * (def.segments[i].spawn_position - pivot);
        rotations[i] = glm::normalize(turn * def.segments[i].spawn_rotation);
    }
    if(on_field) {
        // The standing pose stands on y = 0: onto the ground, as low as its soles allow
        float raise = -1e9f;
        for(size_t i = 0; i < def.segments.size(); i++) {
            if(!body.is_foot[i]) continue;
            const ShapeDef& shape = def.segments[i].shapes[0];
            for(int c = 0; c < 4; c++) {
                glm::vec3 corner = positions[i] + rotations[i] * (shape.center + shape.rotation * (shape.half_extents * glm::vec3(c & 1 ? 1.f : -1.f, -1.f, c & 2 ? 1.f : -1.f)));
                raise = std::max(raise, bot.ground->height(corner.x, corner.z, corner.y + 2.f * body.height, corner.y) - corner.y);
            }
        }
        if(raise > -1e8f) for(auto& p : positions) p.y += raise + 0.002f;
    }
    for(size_t i = 0; i < def.segments.size(); i++) {
        bodies.SetPositionRotationAndVelocity(bot.parts.bodies[i], JPH::RVec3(toJolt(positions[i])), toJolt(rotations[i]), JPH::Vec3::sZero(), JPH::Vec3::sZero());
    }
    for(auto& c : bot.parts.constraints) c->ResetWarmStart();
    readAnimalState(reg, bot);
    for(size_t i = 0; i < def.segments.size(); i++) {
        bot.origin_previous[i] = bot.position[i];
        bot.origin_velocity[i] = glm::vec3(0.f);
    }
    bot.history_valid = true;
}

void dy::driveAnimalBot(AnimalBot& bot, const float* action, float strength) {
    const AnimalBody& body = *bot.body;
    for(size_t j = 0; j < body.def.joints.size(); j++) {
        const JointDef& joint = body.def.joints[j];
        const AnimalJointDrive& drive = body.drives[j];
        auto* c = bot.parts.constraints[j].GetPtr();
        const float* a = action + animal_actions_per_joint * j;
        auto value = [&](int i) { float v = a[i]; return std::isfinite(v) ? std::clamp(v, -1.f, 1.f) : 0.f; };

        // Gains never reach zero: a spring without stiffness is a rigid one to Jolt
        float spring = 0.02f + 0.98f * (value(0) + 1.f) * 0.5f, damper = 0.02f + 0.98f * (value(1) + 1.f) * 0.5f;
        for(auto* motor : { &c->GetSwingMotorSettings(), &c->GetTwistMotorSettings() }) {
            motor->mSpringSettings.mStiffness = drive.stiffness * spring * strength;
            motor->mSpringSettings.mDamping = drive.damping * damper;
            motor->SetTorqueLimit(drive.max_torque * strength);
        }
        JPH::EMotorState state = bot.limp ? JPH::EMotorState::Off : JPH::EMotorState::PositionAndVelocity;
        if(c->GetSwingMotorState() != state) {
            c->SetSwingMotorState(state);
            c->SetTwistMotorState(state);
            // Limp: no drive, the little friction a dead joint has (without any a hanging leg swings for ever)
            c->SetMaxFrictionTorque(bot.limp ? 0.0005f * drive.max_torque : 0.f);
        }

        // Each component from its limit to its limit through the standing pose: a zero action stands
        float twist = piece(value(2), joint.twist_min, drive.stand.x, joint.twist_max);
        float swing_y = piece(value(3), -joint.swing_y, drive.stand.y, joint.swing_y);
        float swing_z = piece(value(4), -joint.swing_z, drive.stand.z, joint.swing_z);
        glm::quat q = constraintRotation(twist, swing_y, swing_z);
        c->SetTargetOrientationCS(toJolt(q));
        JPH::Quat applied = c->GetTargetOrientationCS();
        if(std::abs(glm::dot(toGlm(applied), q)) < 0.99999f) bot.target_clamps++;

        glm::vec3 omega(value(5), value(6), value(7));
        c->SetTargetAngularVelocityCS(toJolt(omega * body.max_target_angular_velocity));
    }
    bot.decisions++;
    bot.window_ticks = 0;
}

void dy::syncAnimalBot(entt::registry& reg, AnimalBot& bot, double h) {
    readAnimalState(reg, bot);
    for(size_t i = 0; i < bot.position.size(); i++) {
        if(bot.history_valid && h > 0.) bot.origin_velocity[i] = (bot.position[i] - bot.origin_previous[i]) / (float) h;
        else bot.origin_velocity[i] = glm::vec3(0.f);
        bot.origin_previous[i] = bot.position[i];
    }
    bot.history_valid = true;
    bot.tick++;
    bot.window_ticks++;
}

void dy::loadAnimalAssets(AnimalBot& bot, const AnimalSpawn& spawn) {
    bot.spawn = spawn;
    bot.body = loadAnimalBody(animalBodyPath(spawn.body));
    bot.base_params = loadAnimalParams(libraryResourceFile(spawn.params).string());
    {
        const std::string path = libraryResourceFile(spawn.ranges).string();
        std::ifstream in(path, std::ios::binary);
        if(!in) throw AssetError(path + ": cannot open");
        bot.ranges = GaitRanges::parse(nlohmann::json::parse(in));
        bot.ranges_hash = fileSha256(path);
    }
    bot.kinematic = spawn.kinematic;
    bot.recording = spawn.recording;
    bot.draw = spawn.draw;
    bot.view = spawn.view;
    bot.view_truncate = spawn.view_truncate;
}

AnimalParams dy::parseAnimalParams(const nlohmann::json& j) {
    AnimalParams p;
    if(j.value("format_version", 0) != 1 || j.value("kind", "") != "animal_params") throw AssetError("animal params: not an animal_params file of format 1");
    p.max_frames_60hz = j.value("max_frames_60hz", p.max_frames_60hz);
    p.root_reward = j.value("root_reward", p.root_reward);
    p.death_penalty = j.value("death_penalty", p.death_penalty);
    p.life_reward = j.value("life_reward", p.life_reward);
    p.reward_factor = j.value("reward_factor", p.reward_factor);
    p.die_on_fall = j.value("die_on_fall", p.die_on_fall);
    p.fall_height = j.value("fall_height", p.fall_height);
    p.touch_margin = j.value("touch_margin", p.touch_margin);
    p.random_yaw = j.value("random_yaw", p.random_yaw);
    p.soft_resistance = j.value("soft_resistance", p.soft_resistance);
    p.soft_lift = j.value("soft_lift", p.soft_lift);
    p.head_reward = j.value("head_reward", p.head_reward);
    p.mode_reward = j.value("mode_reward", p.mode_reward);
    return p;
}

std::shared_ptr<const AnimalParams> dy::loadAnimalParams(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if(!in) throw AssetError(path + ": cannot open");
    auto p = std::make_shared<AnimalParams>(parseAnimalParams(nlohmann::json::parse(in)));
    p->file_hash = fileSha256(path);
    return p;
}

glm::vec3 dy::lowestPoint(const SegmentDef& segment, glm::vec3 position, glm::quat rotation) {
    glm::vec3 lowest(0.f, 1e9f, 0.f);
    for(const ShapeDef& s : segment.shapes) {
        glm::vec3 centre = position + rotation * s.center;
        if(s.type == ShapeDef::Type::Box) {
            glm::quat r = rotation * s.rotation;
            for(int c = 0; c < 8; c++) {
                glm::vec3 corner = centre + r * (s.half_extents * glm::vec3(c & 1 ? 1.f : -1.f, c & 2 ? 1.f : -1.f, c & 4 ? 1.f : -1.f));
                if(corner.y < lowest.y) lowest = corner;
            }
        } else {
            glm::vec3 p = centre;
            if(s.type == ShapeDef::Type::Capsule) {
                glm::vec3 axis = rotation * (s.rotation * glm::vec3(0.f, s.half_height, 0.f));
                p = axis.y < 0.f ? centre + axis : centre - axis;
            }
            p.y -= s.radius;
            if(p.y < lowest.y) lowest = p;
        }
    }
    return lowest;
}

float AnimalBot::groundUnder(glm::vec3 p) const {
    // The kinematic walk wades through soft ground: its field is the surface, the hard ground lies soft_depth under
    return ground ? ground->height(p.x, p.z, p.y + 1.f, p.y) - (kinematic ? soft_depth : 0.f) : 0.f;
}

float AnimalBot::trunkHeight() const {
    return position[0].y - groundUnder(position[0]);
}

int AnimalBot::fallen(int* segment) const {
    if(trunkHeight() < params.fall_height * body->stand_height) return 1;
    const float margin = params.touch_margin * body->height;
    for(size_t s = 0; s < position.size(); s++) {
        if(body->may_touch[s]) continue;
        glm::vec3 low = lowestPoint(body->def.segments[s], position[s], rotation[s]);
        if(low.y - groundUnder(glm::vec3(low.x, low.y + 0.5f * body->height, low.z)) < margin) {
            if(segment) *segment = (int) s;
            return 2;
        }
    }
    return 0;
}

void dy::observeAnimal(const AnimalBot& bot, float* out) {

    const AnimalBody& body = *bot.body;
    const float v0 = std::max(body.walk_speed, 1e-3f);
    HeadingFrame frame = headingFrame(bot.rotation[0]);
    glm::vec3 command = bot.command.direction() * bot.command.speed;
    glm::vec3 d = command - horizontal(bot.origin_velocity[0]);
    out[0] = glm::dot(frame.x, command) / v0;
    out[1] = glm::dot(frame.z, command) / v0;
    out[2] = bot.command.turn * body.length;
    out[3] = glm::dot(frame.x, d) / v0;
    out[4] = glm::dot(frame.z, d) / v0;
    out[5] = std::clamp(1.f - bot.trunkHeight() / body.stand_height, -1.f, 1.f);
    out[6] = bot.soft_depth / body.height;

    for(size_t s = 0; s < bot.position.size(); s++) {
        glm::vec3 n = frame.project(bot.rotation[s] * glm::vec3(0.f, 0.f, -1.f));
        glm::vec3 t = frame.project(bot.rotation[s] * glm::vec3(0.f, 1.f, 0.f));
        glm::vec3 u = frame.project(bot.origin_velocity[s]) / v0;
        float* o = out + animal_header + 9 * s;
        o[0] = n.x; o[1] = n.y; o[2] = n.z;
        o[3] = t.x; o[4] = t.y; o[5] = t.z;
        o[6] = u.x; o[7] = u.y; o[8] = u.z;
    }

}

void dy::resetAnimalBot(entt::registry& reg) {

    auto& bot = reg.ctx().get<AnimalBot>();
    const AnimalBody& body = *bot.body;
    const BodyDef& def = body.def;
    auto& rng = reg.ctx().get<WorldRng>();

    applyEnvParams(reg, bot);
    bot.route_lost = false;

    if(bot.kinematic) {
        // The player starts another walk at its next tick
        bot.demo_restart = true;
    } else {

        // The depth of soft ground this episode: the ranges', or the scene's
        bot.soft_depth = 0.f;
        if(bot.spawn.soft_depth >= 0.f) bot.soft_depth = bot.spawn.soft_depth * body.height;
        else if(bot.ranges.soft > 0.f && rng.uniform() < bot.ranges.soft) bot.soft_depth = (float) rng.uniform(bot.ranges.low.soft_depth, std::max(bot.ranges.low.soft_depth, bot.ranges.high.soft_depth)) * body.height;

        const bool routed = bot.routes && bot.ground;
        float yaw = bot.params.random_yaw && (!bot.view || bot.view_truncate) ? (float) rng.uniform(0., 2. * pi) : 0.f;
        bot.origin = glm::vec3(0.f);
        if(routed) {
            RouteStart start = drawStart(*bot.ground, rng, *bot.routes);
            bot.origin = start.point;
            yaw = std::atan2(-start.direction.x, -start.direction.z);
            if(!start.found) dy::log(dy::warning) << "AnimalBot: no spawn point in the slope band after " << start.tries << " draws\n";
        }
        standAnimalBot(reg, bot, bot.origin, yaw, routed);
        beginCommand(reg, bot, yaw, body.walk_speed);
    }

    std::fill(bot.previous_frame.begin(), bot.previous_frame.end(), 0.f);
    bot.snapshot_trunk = bot.position[0];
    bot.snapshot_direction = bot.command.direction();
    bot.snapshot_speed = bot.command.speed;
    bot.mode = bot.snapshot_mode = 0;
    bot.head = bot.snapshot_head = glm::vec2(0.f);
    bot.order_until = 0.f;
    if(bot.orders && !bot.kinematic) {
        drawOrder(bot, rng);
        bot.command.wanted_speed *= orderSpeed(bot.mode);
        bot.command.speed = bot.command.wanted_speed;
        bot.snapshot_speed = bot.command.speed;
        bot.snapshot_mode = bot.mode;
        bot.snapshot_head = bot.head;
    }

    bot.tick = bot.decisions = bot.window_ticks = 0;
    bot.killed = bot.truncated = bot.pending_reset = false;
    bot.fall_cause = 0;
    bot.fall_segment = -1;
    bot.fresh = true;
    bot.cumulative_reward = 0.;
    bot.episode = AnimalEpisode();
    bot.episode.soft_depth = bot.soft_depth;

    auto& io = agentIO(reg);
    std::fill(io.action.begin(), io.action.end(), 0.f);
    io.reward = 0.f;
    io.done = io.truncated = false;
    io.stats.clear();
    setPark(reg, false);

}

// AnimalFactory

void AnimalFactorySys::preinit() {
    agentIO(reg);
}

void AnimalFactorySys::init() {

    auto view = reg.view<AnimalSpawn>();
    if(view.begin() == view.end()) {
        dy::log(dy::error) << "AnimalFactory: no entity with AnimalSpawn\n";
        return;
    }
    const auto& spawn = reg.get<AnimalSpawn>(*view.begin());

    auto& bot = reg.ctx().emplace<AnimalBot>();
    loadAnimalAssets(bot, spawn);
    if(auto* field = reg.ctx().find<std::shared_ptr<GroundField>>()) bot.ground = *field;
    if(auto* routes = reg.try_get<WalkRoutes>(*view.begin())) bot.routes = *routes;
    applyEnvParams(reg, bot);

    buildAnimalRagdoll(reg, bot);
    if(reg.ctx().contains<std::shared_ptr<TerrainColliders>>()) reg.ctx().insert_or_assign(TerrainUsers { bot.parts.bodies });
    const AnimalBody& body = *bot.body;
    const BodyDef& def = body.def;
    const size_t n = def.segments.size();

    auto& io = agentIO(reg);
    io.configure(body.behavior, bot.observationSize(), bot.actionSize());
    io.sensor_name = "StackingSensor_size2_VectorSensor_size" + std::to_string(bot.frame_size);
    bot.orders = spawn.orders;
    io.sensors.clear();
    if(bot.orders) io.addVector("nogail_order", animal_order_size);

    if(bot.draw) {
        for(size_t i = 0; i < n; i++) {
            for(const ShapeDef& s : def.segments[i].shapes) {
                auto e = reg.create();
                reg.emplace<Transform>(e);
                auto& shape = reg.emplace<WorldShape>(e);
                shape.segment = (int) i;
                shape.local_position = s.center;
                shape.local_rotation = s.rotation;
                if(s.type == ShapeDef::Type::Capsule) { shape.type = WorldShape::Type::Capsule; shape.half_extent = glm::vec3(s.radius, s.half_height, s.radius); }
                else if(s.type == ShapeDef::Type::Sphere) { shape.type = WorldShape::Type::Sphere; shape.half_extent = glm::vec3(s.radius); }
                else { shape.type = WorldShape::Type::Box; shape.half_extent = s.half_extents; }
                float hue = 0.35f + 0.5f * ((i * 37) % 16) / 16.f;
                reg.emplace<ModelRef>(e, ModelRef { "", glm::vec4(hue, 0.55f, 1.05f - hue, 1.f) });
            }
        }
    }

    resetAnimalBot(reg);

}

void AnimalFactorySys::finish() {
    auto* bot = reg.ctx().find<AnimalBot>();
    if(bot) destroyAnimalRagdoll(reg, *bot);
}

// AnimalDrive

void AnimalDriveSys::tick(double h) {

    auto& bot = reg.ctx().get<AnimalBot>();
    auto& io = agentIO(reg);
    if(!bot.built || bot.kinematic) return;
    driveAnimalBot(bot, io.action.data());

}

// AnimalSync

void AnimalSyncSys::tick(double h) {

    auto& bot = reg.ctx().get<AnimalBot>();
    if(!bot.built) return;
    syncAnimalBot(reg, bot, h);

    if(bot.draw) {
        for(auto [e, shape, transform] : reg.view<WorldShape, Transform>().each()) {
            glm::vec3 position = bot.position[shape.segment] + bot.rotation[shape.segment] * shape.local_position;
            glm::quat rotation = glm::normalize(bot.rotation[shape.segment] * shape.local_rotation);
            auto* pose = reg.try_get<PhysicsPose>(e);
            if(!pose) pose = &reg.emplace<PhysicsPose>(e, PhysicsPose { position, position, rotation, rotation });
            pose->prev_position = pose->position;
            pose->prev_rotation = pose->rotation;
            pose->position = position;
            pose->rotation = rotation;
            transform.position = position;
            transform.rotation = rotation;
        }
    }

}

// AnimalTask

void AnimalTaskSys::tick(double h) {
    auto& bot = reg.ctx().get<AnimalBot>();
    if(!bot.built || bot.kinematic) return;
    const float before = bot.command.until;
    advanceCommand(reg, bot, bot.position[0], bot.body->walk_speed, (float) h);
    if(!bot.orders) return;
    // The order changes at its own pace; a mode that stands still holds the speed of every command drawn under it
    const bool redrawn = bot.command.until != before;
    if(bot.command.time >= bot.order_until) {
        const float full = orderSpeed(bot.mode) > 0.f ? bot.command.wanted_speed / orderSpeed(bot.mode) : -1.f;
        drawOrder(bot, reg.ctx().get<WorldRng>());
        // The command in force is rescaled to the new mode (from a mode that stood still: drawn again)
        if(full >= 0.f) bot.command.wanted_speed = full * orderSpeed(bot.mode);
        else bot.command.until = bot.command.time;
    } else if(redrawn) {
        bot.command.wanted_speed *= orderSpeed(bot.mode);
    }
}

// AnimalTermination

void AnimalTerminationSys::tick(double h) {

    auto& bot = reg.ctx().get<AnimalBot>();
    if(!bot.built || bot.kinematic) return;

    if(!bot.immortal) {
        int segment = -1;
        int cause = bot.fallen(&segment);
        if(cause && bot.params.die_on_fall) {
            bot.killed = true;
            bot.fall_cause = cause;
            bot.fall_segment = segment;
            setPark(reg, true);
            return;
        }
    }
    if(bot.tick >= episodeTicks(reg, bot) || bot.route_lost) bot.truncated = true;

}

// AnimalRewards

void AnimalRewardsSys::tick(double h) {

    auto& bot = reg.ctx().get<AnimalBot>();
    auto& io = agentIO(reg);
    if(!bot.built) return;

    if(io.reset_requested || bot.pending_reset) {
        io.reset_requested = false;
        resetAnimalBot(reg);
        return;
    }

    const auto& clock = reg.ctx().get<WorldClock>();
    const AnimalParams& p = bot.params;
    const AnimalBody& body = *bot.body;
    const double window = std::max(bot.window_ticks, 1) * clock.dt;
    const glm::vec3 trunk = bot.position[0];
    const AnimalRating rating = rateAnimal(bot, window);
    const glm::vec3 velocity = rating.velocity;
    const float along = rating.along;
    const float root = rating.root;

    float reward = p.life_reward + root + rating.head + rating.mode;

    io.stats.clear();
    auto stat = [&](const char* key, float value, int aggregation = 0) { io.stats.emplace_back(key, value, aggregation); };
    const int histogram = 3;
    const float heading_error = rating.heading_error;
    stat("Root/Root Speed", along, histogram);
    stat("Root/Root Damper Reward", root, histogram);
    stat("Root/Root Horizontal Velocity", glm::length(horizontal(velocity)), histogram);
    stat("Animal/Speed Ratio", bot.snapshot_speed > 0.f ? along / bot.snapshot_speed : 0.f, histogram);
    stat("Animal/Commanded Speed", bot.snapshot_speed, histogram);
    stat("Animal/Heading Error", glm::degrees(heading_error), histogram);
    stat("Animal/Trunk Height", bot.trunkHeight() / body.stand_height, histogram);
    stat("Animal/Target Clamped", (float) bot.target_clamps);
    if(bot.soft_depth > 0.f) stat("Animal/Soft Depth", bot.soft_depth / body.height);
    if(bot.orders) {
        stat("Order/Head Reward", rating.head);
        stat("Order/Head Error", glm::degrees(rating.head_error));
        stat(("Order/Reward " + std::string(animalModeName(bot.snapshot_mode))).c_str(), rating.mode);
        if(bot.snapshot_mode == (int) AnimalMode::Feed) stat("Order/Feed Muzzle Height", rating.muzzle_height / body.stand_height);
    }
    stat("Death/Alive", bot.killed ? 0.f : 1.f);

    bot.episode.distance += glm::length(horizontal(trunk - bot.snapshot_trunk));
    bot.episode.progress += along * (float) window;
    bot.episode.commanded += bot.snapshot_speed * (float) window;
    bot.episode.heading_error += heading_error;

    if(bot.killed) {
        // Death takes the whole reward of the step: a fall costs what would have followed it, nothing more
        // (death_penalty, 0 by default, comes on top)
        reward = p.death_penalty;
        stat("Death/Death Frames", (float) bot.tick, histogram);
        stat("Animal/Fall Trunk Low", bot.fall_cause == 1 ? 1.f : 0.f);
        stat("Animal/Fall Body Touch", bot.fall_cause == 2 ? 1.f : 0.f);
    }
    if(bot.killed || bot.truncated) {
        stat("Win/Won", bot.killed ? 0.f : 1.f);
        stat("Death/Dead", bot.killed ? 1.f : 0.f);
        stat("Animal/Episode Speed Ratio", bot.episode.commanded > 0.f ? bot.episode.progress / bot.episode.commanded : 0.f);
        if(bot.routes && bot.ground) stat("Terrain/Route Lost", bot.route_lost && !bot.killed ? 1.f : 0.f);
    }

    io.reward = reward * p.reward_factor;
    bot.cumulative_reward += io.reward;

}

// AnimalObservation

void AnimalObservationSys::tick(double h) {

    auto& bot = reg.ctx().get<AnimalBot>();
    auto& io = agentIO(reg);
    if(!bot.built) return;

    // The command of the next window; the observation reads the same one
    bot.snapshot_trunk = bot.position[0];
    bot.snapshot_direction = bot.command.direction();
    bot.snapshot_speed = bot.command.speed;
    bot.snapshot_mode = bot.mode;
    bot.snapshot_head = bot.head;
    if(bot.orders && !io.sensors.empty()) observeOrder(bot, io.sensors[0].data.data());

    if(bot.recording) {
        io.recordable = bot.warm_left <= 0 && !bot.demo_restart;
        // A recorded walk starts on an empty stack, as an episode does
        if(io.recordable && bot.recorded == 0) std::fill(bot.previous_frame.begin(), bot.previous_frame.end(), 0.f);
        if(io.recordable) bot.recorded++;
    }

    std::vector<float> current(bot.frame_size);
    observeAnimal(bot, current.data());
    for(float& f : current) if(!std::isfinite(f)) f = 0.f;
    std::copy(bot.previous_frame.begin(), bot.previous_frame.end(), io.observation.begin());
    std::copy(current.begin(), current.end(), io.observation.begin() + bot.frame_size);
    bot.previous_frame = current;

}

// AnimalEpisode

void AnimalEpisodeSys::tick(double h) {

    auto& bot = reg.ctx().get<AnimalBot>();
    auto& io = agentIO(reg);
    if(!bot.built) return;

    if(bot.fresh) {
        bot.fresh = false;
        io.done = false;
        io.truncated = false;
        return;
    }
    if(bot.recording && bot.recorded >= bot.spawn.recording_decisions) bot.truncated = true;
    io.done = bot.killed || bot.truncated;
    io.truncated = bot.truncated && !bot.killed;
    if(io.done) {
        bot.episode.ticks = bot.tick;
        bot.episode.fell = bot.killed;
        bot.episode.cause = bot.fall_cause;
        bot.episode.segment = bot.fall_segment;
        if(bot.decisions > 0) bot.episode.heading_error /= (float) bot.decisions;
        if(bot.view_truncate) bot.episodes.push_back(bot.episode);
        bot.pending_reset = true;
        setPark(reg, true);
    }

}

// AnimalSoftGround

glm::vec3 dy::softGroundForce(const AnimalParams& params, float mass, float walk_speed, float inside, glm::vec3 velocity, float h) {
    inside = std::clamp(inside, 0.f, 1.f);
    // Scaled by the size of the animal: the same in walk speeds for a cat and a horse
    const float k = params.soft_resistance * mass * 9.81f * inside / std::max(walk_speed, 0.05f);
    glm::vec3 force(-k * velocity.x, velocity.y > 0.f ? -k * params.soft_lift * velocity.y : 0.f, -k * velocity.z);
    // Never more, on any axis, than what stops the body within the step
    const float most = mass / std::max(h, 1e-6f);
    for(int a = 0; a < 3; a++) {
        if(std::abs(force[a]) > most * std::abs(velocity[a])) force[a] = -most * velocity[a];
    }
    return force;
}

void AnimalSoftGroundSys::tick(double h) {

    auto& bot = reg.ctx().get<AnimalBot>();
    if(!bot.built || bot.kinematic || bot.soft_depth <= 0.f) return;
    // The hard ground lies soft_depth under the surface. A body inside the layer is held by a drag on its origin's
    // velocity, in weights of the body per m/s: full sideways, a part of it upwards, so that pulling a foot straight
    // out is the cheap way. Downwards the layer does not hold: the foot sinks to the hard ground
    auto& bodies = physicsWorld(reg).bodies();
    const AnimalBody& body = *bot.body;
    for(size_t s = 0; s < bot.position.size(); s++) {
        glm::vec3 low = lowestPoint(body.def.segments[s], bot.position[s], bot.rotation[s]);
        float depth = bot.soft_depth - (low.y - bot.groundUnder(glm::vec3(low.x, low.y + 0.5f * body.height, low.z)));
        if(depth <= 0.f) continue;
        float mass = body.mass * body.def.segments[s].percentage / 100.f;
        glm::vec3 force = softGroundForce(bot.params, mass, body.walk_speed, depth / bot.soft_depth, bot.linear[s], (float) h);
        bodies.AddForce(bot.parts.bodies[s], toJolt(force));
    }

}

// AnimalGaitPlayer

void AnimalGaitPlayerSys::init() {

    auto* bot = reg.ctx().find<AnimalBot>();
    auto& state = reg.ctx().emplace<AnimalWalkState>();
    if(!bot || !bot->built) {
        state.problem = "no animal";
        return;
    }
    if(!bot->kinematic) {
        state.problem = "the body is not kinematic";
        dy::log(dy::error) << "AnimalGaitPlayer: " << state.problem << "\n";
        return;
    }
    try {
        auto species = AnimalLibrary::loadSpecies(animalsDirectory(), bot->body->species);
        int clip = bot->spawn.gait == "run" ? species->run : bot->spawn.gait == "trot" ? species->trot : species->walk;
        if(clip < 0) state.problem = bot->body->species + " has no " + bot->spawn.gait;
        else if(!state.walk.setup(species, clip, bot->body->length)) state.problem = state.walk.model.problem;
        else {
            state.bind = animalBodyBind(*bot->body, *species);
            state.ready = true;
        }
    } catch(const std::exception& e) {
        state.problem = e.what();
    }
    if(!state.ready) {
        dy::log(dy::error) << "AnimalGaitPlayer: " << state.problem << "\n";
        return;
    }
    if(!bot->ground) {
        state.plane = std::make_shared<PlaneField>();
        bot->ground = state.plane;
    }
    bot->demo_restart = true;

}

void AnimalGaitPlayerSys::tick(double h) {

    auto& bot = reg.ctx().get<AnimalBot>();
    auto& state = reg.ctx().get<AnimalWalkState>();
    if(!state.ready) return;
    auto& rng = reg.ctx().get<WorldRng>();
    auto& bodies = physicsWorld(reg).bodies();
    const AnimalBody& body = *bot.body;
    GaitWalk& gw = state.walk;
    const float dt = (float) h;
    const float clip_speed = commandSpeed(bot, &state);

    bool teleport = false;
    if(bot.demo_restart) {
        bot.demo_restart = false;
        bot.walks++;
        GaitStyle style = drawStyle(bot.ranges, body.height, rng, bot.spawn.soft_depth >= 0.f ? (bot.spawn.soft_depth > 0.f ? 1 : 0) : -1);
        if(bot.spawn.soft_depth >= 0.f) style.soft_depth = bot.spawn.soft_depth * body.height;
        bot.soft_depth = style.soft_depth;
        glm::vec3 from(0.f);
        float yaw = (float) rng.uniform(0., 2. * pi);
        if(bot.spawn.heading >= 0.f) yaw = glm::radians(bot.spawn.heading);
        bot.route_lost = false;
        if(bot.routes && bot.ground) {
            RouteStart start = drawStart(*bot.ground, rng, *bot.routes);
            from = start.point;
            yaw = std::atan2(-start.direction.x, -start.direction.z);
        }
        bot.origin = from;
        gw.start(style, from, yaw, (float) rng.uniform());
        beginCommand(reg, bot, yaw, clip_speed);
        const auto& clock = reg.ctx().get<WorldClock>();
        const int period = (int) clock.decision_period;
        int warm = (int) std::lround((bot.spawn.warmup + rng.uniform() * gw.model.period) / dt);
        bot.warm_left = std::max((warm / period) * period, period);
        bot.recorded = 0;
        teleport = true;
    }

    // The decision windows of a body no drive counts
    if(reg.ctx().get<WorldClock>().tick % reg.ctx().get<WorldClock>().decision_period == 0) {
        bot.decisions++;
        bot.window_ticks = 0;
    }
    advanceCommand(reg, bot, gw.transform.position, clip_speed, dt);
    gw.step(bot.command.speed, bot.command.turn, bot.ground.get(), dt);
    // The walker turned as the command did: one heading
    bot.command.yaw = gw.walker.yaw;

    std::vector<glm::vec3> positions;
    std::vector<glm::quat> rotations;
    segmentPoses(body, state.bind, gw.pose.world, gw.transform.matrix(), positions, rotations);
    for(size_t s = 0; s < positions.size(); s++) {
        JPH::RVec3 p(toJolt(positions[s]));
        JPH::Quat q = toJolt(rotations[s]);
        if(teleport) bodies.SetPositionAndRotation(bot.parts.bodies[s], p, q, JPH::EActivation::Activate);
        else bodies.MoveKinematic(bot.parts.bodies[s], p, q, dt);
    }
    if(teleport) bot.history_valid = false;
    if(bot.warm_left > 0) bot.warm_left--;

}

// AnimalChecks

nlohmann::json AnimalDemoChecks::json() const {
    return { { "ticks", ticks }, { "decisions", decisions }, { "limit_excess_deg", glm::degrees(limit_excess) }, { "limit_joint", limit_joint },
        { "limit_ticks", limit_ticks }, { "axis_jump", axis_jump }, { "velocity_jump", velocity_jump }, { "axis_jumps", axis_jumps }, { "heading_flips", heading_flips },
        { "lowest", lowest }, { "axis_segment", axis_segment }, { "velocity_segment", velocity_segment }, { "lowest_segment", lowest_segment }, { "falls", falls }, { "fall_cause", fall_cause }, { "fall_segment", fall_segment },
        { "speed_ratio", commanded > 0. ? progress / commanded : 0. }, { "speed_error", speed_error }, { "stretch", stretch }, { "foot_slide", foot_slide } };
}

void AnimalChecksSys::preinit() {
    reg.ctx().emplace<AnimalDemoChecks>();
}

void AnimalChecksSys::tick(double h) {

    auto& bot = reg.ctx().get<AnimalBot>();
    auto& checks = reg.ctx().get<AnimalDemoChecks>();
    auto* state = reg.ctx().find<AnimalWalkState>();
    if(!bot.built || bot.warm_left > 0 || bot.demo_restart) return;
    const AnimalBody& body = *bot.body;
    const auto& clock = reg.ctx().get<WorldClock>();

    checks.ticks++;
    float worst = 0.f;
    for(size_t j = 0; j < body.def.joints.size(); j++) {
        const JointDef& joint = body.def.joints[j];
        float excess = limitExcess(body, (int) j, jointAngles(body, (int) j, bot.rotation[joint.parent], bot.rotation[joint.child]));
        if(excess > checks.limit_excess) {
            checks.limit_excess = excess;
            checks.limit_joint = (int) j;
        }
        worst = std::max(worst, excess);
    }
    if(worst > glm::radians(0.5f)) checks.limit_ticks++;
    for(size_t s = 0; s < bot.position.size(); s++) {
        glm::vec3 low = lowestPoint(body.def.segments[s], bot.position[s], bot.rotation[s]);
        // In soft ground the feet are under the surface: measured from the hard ground they stand on
        float over = low.y - bot.groundUnder(glm::vec3(low.x, low.y + 0.5f * body.height, low.z));
        if(over < checks.lowest) {
            checks.lowest = over;
            checks.lowest_segment = (int) s;
        }
    }
    {
        int segment = -1;
        // The kinematic walk wades: its ground is the surface, the hard ground of the fall test lies soft_depth under
        int cause = bot.fallen(&segment);
        if(cause) {
            checks.falls++;
            checks.fall_cause = cause;
            checks.fall_segment = segment;
        }
    }
    if(state && state->ready) {
        const GaitWalk& gw = state->walk;
        const glm::mat4 to_world = gw.transform.matrix();
        (void) to_world;
        // How far the pose pulls a joint anchor apart: the child where the walk puts it, against where its parent holds it
        for(size_t s = 1; s < bot.position.size(); s++) {
            const int p = body.def.segments[s].parent;
            checks.stretch = std::max(checks.stretch, glm::distance(bot.position[s], bot.position[p] + bot.rotation[p] * body.offset[s]));
        }
        for(size_t l = 0; l < body.legs.size() && l < gw.gait.feet.size(); l++) {
            if(!gw.gait.feet[l].planted || gw.gait.feet[l].since < 0.05f) continue;
            checks.slides.push_back(glm::length(horizontal(bot.origin_velocity[body.legs[l].segments.back()])));
        }
        if(checks.slides.size() >= 64 && checks.ticks % 256 == 0) {
            std::vector<float> sorted(checks.slides);
            std::sort(sorted.begin(), sorted.end());
            checks.foot_slide = sorted[(size_t) (0.99f * (float) (sorted.size() - 1))];
        }
    }

    if(clock.tick % clock.decision_period != clock.decision_period - 1) return;
    // Before the observe phase: the snapshot is still the last decision's
    const float window = (float) (std::max(bot.window_ticks, 1) * clock.dt);
    if(bot.recorded > 0) {
        float along = glm::dot(bot.position[0] - bot.snapshot_trunk, bot.snapshot_direction);
        checks.progress += along;
        checks.commanded += bot.snapshot_speed * window;
        checks.walk_progress += along;
        checks.walk_commanded += bot.snapshot_speed * window;
    } else {
        if(checks.walk_commanded > 0.) checks.speed_error = std::max(checks.speed_error, (float) std::abs(checks.walk_progress / checks.walk_commanded - 1.));
        checks.walk_progress = checks.walk_commanded = 0.;
        checks.previous.clear();
    }
    std::vector<float> frame(bot.frame_size);
    observeAnimal(bot, frame.data());
    if(!checks.previous.empty()) {
        bool jumped = false;
        for(int i = animal_header; i < bot.frame_size; i++) {
            if((i - animal_header) % 9 < 6 && std::abs(frame[i] - checks.previous[i]) > 0.6f) jumped = true;
            float change = std::abs(frame[i] - checks.previous[i]);
            if((i - animal_header) % 9 < 6) {
                if(change > checks.axis_jump) {
                    checks.axis_jump = change;
                    checks.axis_segment = (i - animal_header) / 9;
                }
            } else if(change > checks.velocity_jump) {
                checks.velocity_jump = change;
                checks.velocity_segment = (i - animal_header) / 9;
            }
        }
        if(jumped) checks.axis_jumps++;
        // The trunk's forward axis in its own heading frame is (cos pitch, sin pitch, 0): a flip shows in the command
        float turned = std::abs(frame[0] * checks.previous[1] - frame[1] * checks.previous[0]);
        float kept = frame[0] * checks.previous[0] + frame[1] * checks.previous[1];
        if(std::atan2(turned, kept) > glm::radians(60.f)) checks.heading_flips++;
    }
    checks.previous = frame;
    checks.decisions++;

}

// The standing test

nlohmann::json AnimalStandResult::json() const {
    return { { "stands", stands }, { "fell", fell }, { "rests", rests }, { "trunk_drop", trunk_drop }, { "trunk_height", trunk_height }, { "drift", drift },
        { "tilt", tilt }, { "max_speed", max_speed }, { "end_speed", end_speed }, { "finite", finite }, { "fall_cause", fall_cause }, { "fall_segment", fall_segment } };
}

AnimalStandResult dy::animalStandTest(const std::string& body_path, float seconds, bool limp) {

    AnimalStandResult result;
    ScenarioRunner runner("animal_stand", { { "body", body_path } }, SceneLoadOptions {}, 1);
    auto& world = runner.world(0);
    auto& bot = world.reg.ctx().get<AnimalBot>();
    const AnimalBody& body = *bot.body;
    bot.immortal = true;
    bot.limp = limp;
    const glm::vec3 start = bot.position[0];
    const glm::quat start_rotation = bot.rotation[0];
    const int ticks = (int) std::lround(seconds * 60.f);
    for(int t = 0; t < ticks; t++) {
        world.tick();
        float fastest = 0.f;
        for(size_t s = 0; s < bot.position.size(); s++) {
            if(!std::isfinite(bot.position[s].x + bot.position[s].y + bot.position[s].z)) result.finite = false;
            fastest = std::max(fastest, glm::length(bot.linear[s]));
        }
        if(!result.finite) break;
        if(t >= ticks / 2) result.max_speed = std::max(result.max_speed, fastest);
        if(t >= ticks - 30) result.end_speed = std::max(result.end_speed, fastest);
        int segment = -1;
        if(int cause = bot.fallen(&segment); cause && !result.fell) {
            result.fell = true;
            result.fall_cause = cause;
            if(segment >= 0) result.fall_segment = body.def.segments[segment].name;
        }
    }
    if(!result.finite) return result;
    result.trunk_drop = start.y - bot.position[0].y;
    result.trunk_height = bot.trunkHeight() / body.stand_height;
    result.drift = glm::length(horizontal(bot.position[0] - start));
    result.tilt = glm::degrees(2.f * std::acos(std::min(std::abs(glm::dot(start_rotation, bot.rotation[0])), 1.f)));
    const float quiet = 0.05f * std::sqrt(9.81f * body.com_height);
    result.stands = !result.fell && std::abs(result.trunk_drop) < 0.08f * body.stand_height && result.drift < 0.05f * std::max(body.length, body.height)
        && result.tilt < 6.f && result.max_speed < quiet;
    result.rests = result.end_speed < 0.1f * std::sqrt(9.81f * body.com_height);
    return result;

}

void dy::registerAnimalAgents() {

    registerComponent("AnimalSpawn", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            AnimalSpawn s;
            JsonFields f(j);
            std::string motion = "dynamic";
            f.read("body", s.body);
            f.read("params", s.params);
            f.read("ranges", s.ranges);
            f.read("gait", s.gait);
            f.read("motion", motion);
            f.read("recording", s.recording);
            f.read("draw", s.draw);
            f.read("view", s.view);
            f.read("view_truncate", s.view_truncate);
            f.read("speed", s.speed);
            f.read("recording_decisions", s.recording_decisions);
            f.read("warmup", s.warmup);
            f.read("soft_depth", s.soft_depth);
            f.read("heading", s.heading);
            f.read("orders", s.orders);
            f.finish();
            if(s.body.empty()) throw ComponentError("/body", "a body file or a species is required");
            if(motion != "dynamic" && motion != "kinematic") throw ComponentError("/motion", "dynamic or kinematic");
            if(s.gait != "walk" && s.gait != "trot" && s.gait != "run") throw ComponentError("/gait", "walk, trot or run");
            s.kinematic = motion == "kinematic";
            reg.emplace_or_replace<AnimalSpawn>(e, s);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& s = reg.get<AnimalSpawn>(e);
            return nlohmann::json { { "body", s.body }, { "params", s.params }, { "ranges", s.ranges }, { "gait", s.gait }, { "motion", s.kinematic ? "kinematic" : "dynamic" },
                { "recording", s.recording }, { "draw", s.draw }, { "view", s.view }, { "view_truncate", s.view_truncate }, { "speed", s.speed },
                { "recording_decisions", s.recording_decisions }, { "warmup", s.warmup }, { "soft_depth", s.soft_depth }, { "heading", s.heading },
                { "orders", s.orders } };
        }
    });

    registerSystem("AnimalFactory", systemInfo<AnimalFactorySys>(SystemScope::World));
    registerSystem("AnimalDrive", systemInfo<AnimalDriveSys>(SystemScope::World));
    registerSystem("AnimalSync", systemInfo<AnimalSyncSys>(SystemScope::World));
    registerSystem("AnimalTask", systemInfo<AnimalTaskSys>(SystemScope::World));
    registerSystem("AnimalTermination", systemInfo<AnimalTerminationSys>(SystemScope::World));
    registerSystem("AnimalRewards", systemInfo<AnimalRewardsSys>(SystemScope::World));
    registerSystem("AnimalObservation", systemInfo<AnimalObservationSys>(SystemScope::World));
    registerSystem("AnimalEpisode", systemInfo<AnimalEpisodeSys>(SystemScope::World));
    registerSystem("AnimalSoftGround", systemInfo<AnimalSoftGroundSys>(SystemScope::World));
    registerSystem("AnimalGaitPlayer", systemInfo<AnimalGaitPlayerSys>(SystemScope::World));
    registerSystem("AnimalChecks", systemInfo<AnimalChecksSys>(SystemScope::World));

}

void dy::applyAnimalEnvParams(entt::registry& reg, AnimalBot& bot) {
    applyEnvParams(reg, bot);
}
