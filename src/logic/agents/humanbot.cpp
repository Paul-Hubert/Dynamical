#include "humanbot.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>

#include "agent_env_params.h"
#include "agent_io.h"
#include "gait.h"
#include "ground_field.h"
#include "logic/components/model_ref.h"
#include "logic/components/transform.h"
#include "physics/physics_sys.h"
#include "physics/physics_world.h"
#include "physics/terrain_colliders.h"
#include "scene/component_registry.h"
#include "scene/system_registry.h"
#include "scene/world.h"
#include "sphere_roller.h"
#include "util/log.h"
#include "util/paths.h"

using namespace dy;

namespace {

constexpr float pi = 3.14159265358979f;

JPH::Mat44 inertiaMatrix(glm::vec3 principal, glm::quat rotation, float scale) {
    JPH::Mat44 r = JPH::Mat44::sRotation(toJolt(rotation));
    JPH::Mat44 d = JPH::Mat44::sScale(toJolt(principal * scale));
    d.SetColumn4(3, JPH::Vec4(0, 0, 0, 1));
    JPH::Mat44 m = r * d * r.Transposed3x3();
    m.SetColumn4(3, JPH::Vec4(0, 0, 0, 1));
    return m;
}

JPH::Ref<JPH::Shape> makeShape(const ShapeDef& s) {
    JPH::Ref<JPH::Shape> shape;
    switch(s.type) {
    case ShapeDef::Type::Capsule: shape = new JPH::CapsuleShape(s.half_height, s.radius); break;
    case ShapeDef::Type::Sphere: shape = new JPH::SphereShape(s.radius); break;
    case ShapeDef::Type::Box: shape = new JPH::BoxShape(toJolt(s.half_extents), std::min({ 0.05f, s.half_extents.x, s.half_extents.y, s.half_extents.z })); break;
    }
    return shape;
}

// The segment's shape in its body frame, its centre of mass where the body file says
JPH::Ref<JPH::Shape> segmentShape(const SegmentDef& seg) {

    JPH::Ref<JPH::Shape> inner;
    if(seg.shapes.size() == 1) {
        const ShapeDef& s = seg.shapes[0];
        inner = new JPH::RotatedTranslatedShape(toJolt(s.center), toJolt(s.rotation), makeShape(s));
    } else {
        JPH::StaticCompoundShapeSettings compound;
        for(const ShapeDef& s : seg.shapes) compound.AddShape(toJolt(s.center), toJolt(s.rotation), makeShape(s));
        auto result = compound.Create();
        if(result.HasError()) throw AssetError("segment " + seg.name + ": " + result.GetError().c_str());
        inner = result.Get();
    }
    JPH::Vec3 offset = toJolt(seg.center_of_mass) - inner->GetCenterOfMass();
    return new JPH::OffsetCenterOfMassShape(inner, offset);

}

void buildRagdoll(entt::registry& reg, HumanBot& bot) {

    auto& world = physicsWorld(reg);
    auto& bodies = world.bodies();
    const BodyDef& def = *bot.body;
    auto& params = bot.params;
    bot.built_mass = params.total_mass;
    const float mass_scale = params.total_mass / def.total_mass;

    JPH::Ref<JPH::GroupFilterTable> filter = new JPH::GroupFilterTable((JPH::uint) def.segments.size());
    for(JPH::uint i = 0; i < def.segments.size(); i++) {
        for(JPH::uint j = i + 1; j < def.segments.size(); j++) filter->DisableCollision(i, j);
    }

    bot.bodies.resize(def.segments.size());
    for(size_t i = 0; i < def.segments.size(); i++) {
        const SegmentDef& seg = def.segments[i];
        JPH::BodyCreationSettings settings(segmentShape(seg), JPH::RVec3(toJolt(seg.rest_position)), toJolt(seg.rest_rotation),
            bot.kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic, PhysicsLayers::agent_layer);
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
        settings.mMassPropertiesOverride.mMass = params.total_mass * seg.percentage / 100.f;
        settings.mMassPropertiesOverride.mInertia = inertiaMatrix(seg.inertia, seg.inertia_rotation, mass_scale);
        settings.mFriction = def.friction;
        settings.mRestitution = def.restitution;
        settings.mLinearDamping = def.linear_damping;
        settings.mAngularDamping = def.angular_damping;
        settings.mMaxAngularVelocity = params.max_angular_velocity;
        settings.mMotionQuality = JPH::EMotionQuality::Discrete;
        settings.mAllowSleeping = false;
        settings.mCollideKinematicVsNonDynamic = bot.kinematic;
        // The marching cubes mesh: a sole that crosses an edge between two triangles must not catch on it
        settings.mEnhancedInternalEdgeRemoval = !bot.kinematic && reg.ctx().contains<std::shared_ptr<TerrainColliders>>();
        settings.mCollisionGroup = JPH::CollisionGroup(filter, 0, (JPH::CollisionGroup::SubGroupID) i);
        bot.bodies[i] = bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
    }

    bot.constraints.clear();
    bot.frame1.resize(def.joints.size());
    bot.frame2.resize(def.joints.size());

    for(size_t j = 0; j < def.joints.size(); j++) {
        const JointDef& joint = def.joints[j];
        const SegmentDef& parent = def.segments[joint.parent];
        const SegmentDef& child = def.segments[joint.child];

        // Constraint frame at the rest pose: X twist, Y plane, Z normal
        glm::vec3 x = glm::normalize(joint.twist_axis), y = glm::normalize(joint.plane_axis - x * glm::dot(joint.plane_axis, x));
        glm::vec3 z = glm::cross(x, y);
        glm::quat constraint = glm::quat_cast(glm::mat3(x, y, z));
        bot.frame1[j] = glm::normalize(glm::inverse(parent.rest_rotation) * constraint);
        bot.frame2[j] = glm::normalize(glm::inverse(child.rest_rotation) * constraint);
        if(bot.kinematic) continue;

        JPH::SwingTwistConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPosition1 = settings.mPosition2 = JPH::RVec3(toJolt(joint.anchor));
        settings.mTwistAxis1 = settings.mTwistAxis2 = toJolt(x);
        settings.mPlaneAxis1 = settings.mPlaneAxis2 = toJolt(y);
        settings.mSwingType = JPH::ESwingType::Cone;
        settings.mPlaneHalfConeAngle = joint.swing_y;
        settings.mNormalHalfConeAngle = joint.swing_z;
        settings.mTwistMinAngle = joint.twist_min;
        settings.mTwistMaxAngle = joint.twist_max;
        // Constraints are made at the rest pose: bodies are still there
        JPH::Ref<JPH::Constraint> c = bodies.CreateConstraint(&settings, bot.bodies[joint.parent], bot.bodies[joint.child]);
        auto* swing = static_cast<JPH::SwingTwistConstraint*>(c.GetPtr());
        for(auto* motor : { &swing->GetSwingMotorSettings(), &swing->GetTwistMotorSettings() }) {
            motor->mSpringSettings.mMode = JPH::ESpringMode::StiffnessAndDamping;
            motor->mSpringSettings.mStiffness = 0.f;
            motor->mSpringSettings.mDamping = 0.f;
        }
        swing->SetSwingMotorState(JPH::EMotorState::PositionAndVelocity);
        swing->SetTwistMotorState(JPH::EMotorState::PositionAndVelocity);
        world.system().AddConstraint(c);
        bot.constraints.emplace_back(swing);
    }

    bot.est0 = def.est0();
    for(int j = 0; j < (int) def.joints.size(); j++) {
        const auto& n = def.joints[j].name;
        if(n == "LeftUpLeg") bot.left_hip = j;
        if(n == "LeftLeg") bot.left_knee = j;
        if(n == "LeftFoot") bot.left_ankle = j;
        if(n == "RightUpLeg") bot.right_hip = j;
        if(n == "RightLeg") bot.right_knee = j;
        if(n == "RightFoot") bot.right_ankle = j;
    }
    bot.built = true;

}

void readState(entt::registry& reg, HumanBot& bot) {
    auto& bodies = physicsWorld(reg).bodies();
    for(size_t i = 0; i < bot.bodies.size(); i++) {
        JPH::RVec3 p;
        JPH::Quat q;
        bodies.GetPositionAndRotation(bot.bodies[i], p, q);
        bot.state.position[i] = toGlm(JPH::Vec3(p));
        bot.state.rotation[i] = toGlm(q);
        bot.state.linear[i] = toGlm(bodies.GetLinearVelocity(bot.bodies[i]));
        bot.state.angular[i] = toGlm(bodies.GetAngularVelocity(bot.bodies[i]));
    }
}

float envGet(entt::registry& reg, const char* name, float fallback) {
    auto* env = reg.ctx().find<AgentEnvParams>();
    return env ? env->get(name, fallback) : fallback;
}

bool envBool(entt::registry& reg, const char* name, bool fallback) {
    return envGet(reg, name, fallback ? 1.f : 0.f) > 0.f;
}

void applyEnvParams(entt::registry& reg, HumanBot& bot) {

    AgentParams p = *bot.base_params;
    p.target_speed = bot.target_speed > 0.f || bot.decisions > 0 ? bot.target_speed : p.target_speed;
    if(!reg.ctx().find<AgentEnvParams>()) { bot.params = p; return; }

    auto f = [&](const char* name, float& v) { v = envGet(reg, name, v); };
    auto b = [&](const char* name, bool& v) { v = envBool(reg, name, v); };
    f("rotationSpring", p.rotation_spring);
    f("rotationDamper", p.rotation_damper);
    f("maxTorque", p.max_torque);
    f("torqueFactor", p.torque_factor);
    f("maxAngularVelocity", p.max_angular_velocity);
    f("maxTargetAngularVelocity", p.max_target_angular_velocity);
    f("totalMass", p.total_mass);
    b("pushing", p.pushing);
    f("pushChance", p.push_chance);
    f("pushForce", p.push_force);
    b("pulling", p.pulling);
    f("pullChance", p.pull_chance);
    f("pullForce", p.pull_force);
    f("collisionPenalty", p.collision_penalty);
    f("rewardFactor", p.reward_factor);
    b("randomTarget", p.random_target);
    b("randomSpeed", p.random_speed);
    b("randomInit", p.random_init);
    f("randomInitChance", p.random_init_chance);
    b("randomize", p.randomize);
    f("rootReward", p.root_reward);
    f("headSpringReward", p.head_spring_reward);
    f("headDamperReward", p.head_damper_reward);
    f("winReward", p.win_reward);
    f("deathPenalty", p.death_penalty);
    f("dyingHeightFactor", p.dying_height_factor);
    f("lifeReward", p.life_reward);
    f("dyingReward", p.dying_reward);
    b("dieOnFall", p.die_on_fall);
    b("resetOnAlive", p.reset_on_alive);
    p.max_dying_frames = (int) envGet(reg, "maxDyingFrames", (float) p.max_dying_frames);
    p.arc_frames_60hz = (int) envGet(reg, "arcFrames", (float) p.arc_frames_60hz);
    b("continuousSpeed", p.continuous_speed);
    f("targetSpeedMin", p.target_speed_min);
    f("targetSpeedMax", p.target_speed_max);
    f("energyBonusWeight", p.energy_weight);
    f("energyScale", p.energy_scale);
    f("energySpeedEdge", p.energy_speed_edge);
    f("energyMinSpeedFloor", p.energy_min_speed_floor);
    f("energyVelocitySmoothingTau", p.energy_tau);
    f("positiveEnergyMultiplier", p.energy_positive_multiplier);
    f("negativeEnergyMultiplier", p.energy_negative_multiplier);
    f("torqueRateWeight", p.torque_rate_weight);
    f("statsSettleSeconds", p.stats_settle_seconds);
    bot.params = p;

}

bool randomizing(const HumanBot& bot) {
    return bot.params.randomize && !bot.recording;
}

void drawSpeed(entt::registry& reg, HumanBot& bot, bool at_reset) {
    auto& rng = reg.ctx().get<WorldRng>();
    AgentParams& p = bot.params;
    if(p.continuous_speed && !bot.recording) {
        float lo = std::max(p.target_speed_min, 0.f);
        if(p.target_speed_max >= lo) {
            bot.continuous_speed_mode = true;
            bot.target_speed = (float) rng.uniform(lo, p.target_speed_max);
            return;
        }
        dy::log(dy::error) << "HumanBot: continuousSpeed is on but targetSpeedMin/Max are unusable\n";
    }
    bot.continuous_speed_mode = false;
    if(randomizing(bot) && p.random_speed && !p.speeds.empty()) {
        double r = rng.uniform();
        for(size_t i = 0; i < p.speeds.size(); i++) {
            if(r <= double(i + 1) / p.speeds.size()) {
                bot.target_speed = p.speeds[i];
                break;
            }
        }
    }
}

// direction: the one the spawn point was drawn with (terrain)
void placeTarget(entt::registry& reg, HumanBot& bot, const glm::vec3* direction = nullptr) {
    glm::vec3 head = bot.state.position[bot.body->head];
    if(bot.routes && bot.ground) {
        // Where the ground stays in the slope band, in training and in eval alike. The target is far: the direction
        // holds while the walker drifts
        glm::vec3 toward;
        if(direction) {
            toward = *direction;
        } else {
            glm::vec3 spine = bot.state.position[bot.body->spine];
            auto drawn = drawDirection(*bot.ground, reg.ctx().get<WorldRng>(), *bot.routes, glm::vec3(spine.x, bot.groundUnder(spine), spine.z));
            if(!drawn) {
                bot.route_lost = true;
                return;
            }
            toward = *drawn;
        }
        bot.target = head + toward * 1000.f;
        return;
    }
    if(randomizing(bot) && bot.params.random_target) {
        auto& rng = reg.ctx().get<WorldRng>();
        float radius = (float) rng.uniform(20., 30.), angle = (float) rng.uniform(0., 2. * pi);
        bot.target = head + glm::vec3(std::cos(angle), 0.f, std::sin(angle)) * radius;
    } else {
        bot.target = head + glm::vec3(0.f, 0.f, -1000.f);
    }
}

void setPark(entt::registry& reg, bool value) {
    if(auto* parked = reg.ctx().find<Parked>()) parked->value = value;
}

int episodeTicks(const entt::registry& reg, const HumanBot& bot) {
    if(bot.recording) return bot.recording_ticks;
    if(bot.view && !bot.view_truncate) return std::numeric_limits<int>::max();
    const auto& clock = reg.ctx().get<WorldClock>();
    double hz = 1. / clock.dt;
    return (int) std::lround(bot.params.max_frames_60hz * hz / 60.);
}

int arcTicks(const entt::registry& reg, const HumanBot& bot) {
    const auto& clock = reg.ctx().get<WorldClock>();
    return (int) std::lround(bot.params.arc_frames_60hz * (1. / clock.dt) / 60.);
}

}

float HumanBot::groundUnder(glm::vec3 p) const {
    return ground ? ground->height(p.x, p.z, p.y + 1.f, p.y) : p.y;
}

float HumanBot::currentHeight() const {
    float head_y = state.position[body->head].y;
    if(!ground) return std::max({ head_y - state.position[body->left_foot].y, head_y - state.position[body->right_foot].y, 0.f });
    glm::vec3 head = state.position[body->head], left = state.position[body->left_foot], right = state.position[body->right_foot];
    float above = head.y - groundUnder(head);
    return std::max({ above - (left.y - groundUnder(left)), above - (right.y - groundUnder(right)), 0.f });
}

namespace {

// Eval: the stretch walked toward one target
void openLeg(HumanBot& bot) {
    if(!bot.view_truncate) return;
    WalkLeg leg;
    leg.from = leg.to = bot.state.position[bot.body->spine];
    leg.direction = safeNormalize(horizontal(bot.target - leg.from));
    leg.strikes = bot.devita.strikes;
    leg.ticks = bot.tick;
    bot.legs.push_back(leg);
    bot.leg_open = true;
}

void closeLeg(HumanBot& bot, bool fell) {
    if(!bot.leg_open || bot.legs.empty()) return;
    WalkLeg& leg = bot.legs.back();
    leg.to = bot.state.position[bot.body->spine];
    leg.strikes = bot.devita.strikes - leg.strikes;
    leg.ticks = bot.tick - leg.ticks;
    leg.fell = fell;
    bot.leg_open = false;
}

}

HeadingFrame dy::headingFrame(glm::quat rotation) {
    glm::vec3 forward = rotation * glm::vec3(0.f, 0.f, -1.f);
    glm::vec3 flat(forward.x, 0.f, forward.z);
    float length = glm::length(flat);
    if(length < 0.1f) {
        // A segment pointing nearly vertical: the right axis, turned +90 degrees about up, gives the heading
        glm::vec3 right = rotation * glm::vec3(1.f, 0.f, 0.f);
        flat = glm::vec3(right.z, 0.f, -right.x);
        length = glm::length(flat);
    }
    HeadingFrame f;
    f.x = length > 1e-6f ? flat / length : glm::vec3(0.f, 0.f, -1.f);
    f.y = glm::vec3(0.f, 1.f, 0.f);
    f.z = glm::cross(f.x, f.y);
    return f;
}

void dy::observeV2(const HumanBot& bot, float* out) {

    const BodyDef& def = *bot.body;
    const RagdollState& s = bot.state;

    glm::vec3 spine = s.position[def.spine];
    glm::vec3 tsv = safeNormalize(horizontal(bot.target - spine)) * bot.target_speed;
    if(bot.dying || bot.recording) tsv = glm::vec3(0.f);
    glm::vec3 v = horizontal(s.linear[def.spine]);
    glm::vec3 d = tsv - v;
    if(bot.recording && bot.params.speed_diff_zero_recording) d = glm::vec3(0.f);

    HeadingFrame frame = headingFrame(s.rotation[def.spine]);
    float head_error = std::clamp(1.f - bot.currentHeight() / bot.est0, 0.f, 1.f);
    out[0] = glm::dot(frame.x, d);
    out[1] = head_error;
    out[2] = glm::dot(frame.z, d);

    for(int j = 0; j < humanbot_joints; j++) {
        const JointDef& joint = def.joints[j];
        HeadingFrame h = headingFrame(s.rotation[def.frameSegment(j)]);
        glm::vec3 n = h.project(s.rotation[joint.child] * joint.unity_axis);
        glm::vec3 t = h.project(s.rotation[joint.child] * joint.unity_secondary);
        glm::vec3 u = h.project(s.anchor_velocity[j]);
        float* o = out + 3 + 9 * j;
        o[0] = n.x; o[1] = n.y; o[2] = n.z;
        o[3] = t.x; o[4] = t.y; o[5] = t.z;
        o[6] = u.x; o[7] = u.y; o[8] = u.z;
    }

}

void dy::resetHumanBot(entt::registry& reg) {

    auto& bot = reg.ctx().get<HumanBot>();
    auto& world = physicsWorld(reg);
    auto& bodies = world.bodies();
    const BodyDef& def = *bot.body;
    auto& rng = reg.ctx().get<WorldRng>();

    // Parameters then the speed (Unity draws first, a config targetSpeed then overrides the draw)
    applyEnvParams(reg, bot);
    if(bot.view) {
        bot.params.randomize = false;
        bot.target_speed = bot.view_speed;
    }
    drawSpeed(reg, bot, true);
    if(auto* env = reg.ctx().find<AgentEnvParams>(); env && env->has("targetSpeed")) {
        bot.target_speed = env->get("targetSpeed", bot.target_speed);
        bot.params.target_speed = bot.target_speed;
    }

    if(bot.recording) {
        // The track plays on through the episodes: only the stack starts over
    } else {
        // The mass and the velocity limit can move between episodes
        const float mass_scale = bot.params.total_mass / def.total_mass;
        if(bot.params.total_mass != bot.built_mass) {
            auto& lock = world.system().GetBodyLockInterface();
            for(size_t i = 0; i < bot.bodies.size(); i++) {
                JPH::BodyLockWrite body(lock, bot.bodies[i]);
                if(!body.Succeeded()) continue;
                JPH::MassProperties mp;
                mp.mMass = bot.params.total_mass * def.segments[i].percentage / 100.f;
                mp.mInertia = inertiaMatrix(def.segments[i].inertia, def.segments[i].inertia_rotation, mass_scale);
                body.GetBody().GetMotionProperties()->SetMassProperties(JPH::EAllowedDOFs::All, mp);
            }
            bot.built_mass = bot.params.total_mass;
        }

        glm::quat yaw(1.f, 0.f, 0.f, 0.f);
        glm::vec3 pivot = def.segments[def.pelvis].spawn_position;
        glm::vec3 lift(0.f);
        glm::quat init(1.f, 0.f, 0.f, 0.f);
        glm::vec3 root_velocity(0.f);
        // On a terrain the episode starts where the ground stays in the slope band, with a first direction that does
        const bool routed = bot.routes && bot.ground;
        RouteStart start;
        bot.route_lost = false;
        if(routed) {
            start = drawStart(*bot.ground, rng, *bot.routes);
            bot.origin = start.point;
            bot.spawn_tries = start.tries;
            if(!start.found) dy::log(dy::warning) << "HumanBot: no spawn point in the slope band after " << start.tries << " draws\n";
        }
        // Placing the target needs the head where the reset leaves it (before the random yaw, as Unity)
        for(size_t i = 0; i < bot.bodies.size(); i++) {
            bot.state.position[i] = def.segments[i].spawn_position;
            bot.state.rotation[i] = def.segments[i].spawn_rotation;
            if(routed) bot.state.position[i] += bot.origin;
        }
        placeTarget(reg, bot, routed ? &start.direction : nullptr);
        applySlope(reg, bot);
        if(randomizing(bot)) {
            if(bot.params.random_target) yaw = glm::angleAxis((float) rng.uniform(0., 2. * pi), glm::vec3(0.f, 1.f, 0.f));
            if(bot.params.random_init && rng.uniform() <= bot.params.random_init_chance) {
                glm::vec3 axis = rng.unitSphere();
                init = glm::angleAxis((float) rng.uniform(0., 2. * pi), glm::normalize(axis + glm::vec3(1e-4f)));
                lift = glm::vec3(0.f, 2.f, 0.f);
                root_velocity = glm::vec3(0.f, -4.f, 0.f);
            }
        } else if(routed) {
            // Eval and view: facing the first direction, as the flat walker faces its target
            yaw = glm::angleAxis(std::atan2(-start.direction.x, -start.direction.z), glm::vec3(0.f, 1.f, 0.f));
        }
        glm::quat turn = glm::normalize(init * yaw);
        // The spawn pose stands on y = 0: on a terrain it goes onto the ground at the spawn point, as low as its
        // soles allow (level soles on a slope: one edge touches)
        glm::vec3 onto(0.f);
        if(routed) {
            onto = bot.origin;
            if(lift.y == 0.f) {
                RigModel model(bot.body);
                float raise = -1e9f;
                for(int side = 0; side < 2; side++) {
                    FootGeometry sole = footGeometry(model, side == 1);
                    const SegmentDef& seg = def.segments[sole.segment];
                    glm::vec3 position = onto + pivot + turn * (seg.spawn_position - pivot);
                    glm::quat rotation = glm::normalize(turn * seg.spawn_rotation);
                    for(int c = 0; c < 4; c++) {
                        glm::vec3 corner = sole.corner(c, position, rotation);
                        raise = std::max(raise, bot.ground->height(corner.x, corner.z, corner.y + 2.f, corner.y) - corner.y);
                    }
                }
                onto.y += raise + 0.002f;
            }
        }
        for(size_t i = 0; i < bot.bodies.size(); i++) {
            const SegmentDef& seg = def.segments[i];
            glm::vec3 position = pivot + lift + turn * (seg.spawn_position - pivot);
            if(routed) position += onto;
            glm::quat rotation = glm::normalize(turn * seg.spawn_rotation);
            bodies.SetPositionRotationAndVelocity(bot.bodies[i], JPH::RVec3(toJolt(position)), toJolt(rotation), toJolt(i == (size_t) def.pelvis ? root_velocity : glm::vec3(0.f)), JPH::Vec3::sZero());
            auto mp = JPH::BodyLockWrite(world.system().GetBodyLockInterface(), bot.bodies[i]);
            if(mp.Succeeded()) mp.GetBody().GetMotionProperties()->SetMaxAngularVelocity(bot.params.max_angular_velocity);
        }
        for(auto& c : bot.constraints) c->ResetWarmStart();
        readState(reg, bot);
        for(int j = 0; j < humanbot_joints; j++) {
            bot.state.anchor_previous[j] = bot.state.position[def.joints[j].child];
            bot.state.anchor_velocity[j] = glm::vec3(0.f);
        }
        bot.state.history_valid = true;
    }

    bot.previous_frame.fill(0.f);
    bot.spawn_spine = bot.state.position[def.spine];
    bot.snapshot.spine = bot.state.position[def.spine];
    bot.snapshot.target_speed = bot.target_speed;
    bot.snapshot.tsv = safeNormalize(horizontal(bot.target - bot.snapshot.spine)) * bot.target_speed;

    bot.tick = bot.decisions = bot.window_ticks = bot.arc_ticks = bot.dying_ticks = 0;
    bot.dying = bot.killed = bot.truncated = bot.pending_reset = false;
    bot.fresh = true;
    bot.cumulative_reward = 0.;
    bot.books.valid = false;
    bot.formulas.valid = false;
    bot.formulas.limb = {};
    bot.books.window_pos = bot.books.window_neg = 0.f;
    bot.books.window_torque_variation = 0.f;
    bot.energy_speed = 0.f;
    bot.energy_speed_valid = false;

    auto& io = agentIO(reg);
    std::fill(io.action.begin(), io.action.end(), 0.f);
    io.reward = 0.f;
    io.done = io.truncated = false;
    io.stats.clear();
    // Published here: earlier, Rewards would clear them, and a killed episode's last windows are parked
    DeVitaBooks& d = bot.devita;
    if(d.stances >= 4 && d.sum_pos > 0.f) {
        io.stats.emplace_back("DeVita/NegPos ratio", d.sum_neg / d.sum_pos, 0);
        io.stats.emplace_back("DeVita/Pos Work per Stance", d.sum_pos / d.stances, 0);
        io.stats.emplace_back("DeVita/Neg Work per Stance", d.sum_neg / d.stances, 0);
        io.stats.emplace_back("DeVita/Accepted Stances", (float) d.stances, 0);
        d.total_pos += d.sum_pos;
        d.total_neg += d.sum_neg;
        d.total_stances += d.stances;
    }
    d.sum_pos = d.sum_neg = 0.f;
    d.stances = 0;
    d.stance = {};
    bot.legs.clear();
    bot.leg_open = false;
    openLeg(bot);
    setPark(reg, false);

}

std::string dy::resourceFile(const std::string& relative) {
    std::filesystem::path local = std::filesystem::path("resources") / relative;
    if(std::filesystem::exists(local)) return local.string();
    return (executableDirectory() / "resources" / relative).string();
}

// AgentFactory

void AgentFactorySys::preinit() {
    agentIO(reg);
}

void AgentFactorySys::init() {

    auto view = reg.view<HumanBotSpawn>();
    if(view.begin() == view.end()) {
        dy::log(dy::error) << "AgentFactory: no entity with HumanBotSpawn\n";
        return;
    }
    const auto& spawn = reg.get<HumanBotSpawn>(*view.begin());

    auto& bot = reg.ctx().emplace<HumanBot>();
    bot.body = loadBody(resourceFile(spawn.body));
    bot.base_params = loadParams(resourceFile(spawn.params));
    bot.kinematic = spawn.kinematic;
    bot.recording = spawn.recording;
    bot.draw = spawn.draw;
    bot.view = spawn.view;
    bot.view_speed = spawn.view_speed;
    bot.view_truncate = spawn.view_truncate;
    bot.recording_ticks = spawn.max_recording_ticks;
    bot.target_speed = bot.base_params->target_speed;
    if(auto* field = reg.ctx().find<std::shared_ptr<GroundField>>()) bot.ground = *field;
    if(auto* routes = reg.try_get<WalkRoutes>(*view.begin())) bot.routes = *routes;
    if(bot.routes && !bot.ground && !bot.kinematic) dy::log(dy::error) << "AgentFactory: WalkRoutes needs a ground field (GroundTerrain or GroundPlane before AgentFactory)\n";
    applyEnvParams(reg, bot);
    bot.params.total_mass = bot.base_params->total_mass;
    buildRagdoll(reg, bot);
    // The terrain's colliders are built around these bodies (WorldTerrain)
    if(reg.ctx().contains<std::shared_ptr<TerrainColliders>>()) reg.ctx().insert_or_assign(TerrainUsers { bot.bodies });

    agentIO(reg).configure(bot.base_params->behavior, humanbot_observations, humanbot_actions);
    agentIO(reg).sensor_name = "StackingSensor_size2_VectorSensor_size138";

    if(bot.draw) {
        for(size_t i = 0; i < bot.body->segments.size(); i++) {
            for(const ShapeDef& s : bot.body->segments[i].shapes) {
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

    resetHumanBot(reg);

}

void AgentFactorySys::finish() {
    auto* bot = reg.ctx().find<HumanBot>();
    if(!bot || !bot->built) return;
    auto& world = physicsWorld(reg);
    for(auto& c : bot->constraints) world.system().RemoveConstraint(c);
    bot->constraints.clear();
    for(auto id : bot->bodies) {
        world.bodies().RemoveBody(id);
        world.bodies().DestroyBody(id);
    }
    bot->bodies.clear();
    bot->built = false;
}

// DriveTargets

void DriveTargetsSys::tick(double h) {

    auto& bot = reg.ctx().get<HumanBot>();
    auto& io = agentIO(reg);
    if(!bot.built || bot.kinematic) return;
    const AgentParams& p = bot.params;

    for(int j = 0; j < humanbot_joints; j++) {
        const JointDef& joint = bot.body->joints[j];
        auto* c = bot.constraints[j].GetPtr();
        const float* a = io.action.data() + 8 * j;
        auto value = [&](int i) { float v = a[i]; return std::isfinite(v) ? std::clamp(v, -1.f, 1.f) : 0.f; };

        float spring = (value(0) + 1.f) * 0.5f, damper = (value(1) + 1.f) * 0.5f;
        float scale = p.torque_factor * joint.torque_factor;
        float stiffness = p.rotation_spring * spring * scale * joint.spring_factor * p.drive_gain_scale;
        float damping = p.rotation_damper * damper * scale * joint.damper_factor * p.drive_gain_scale;
        float limit = p.max_torque * scale;
        for(auto* motor : { &c->GetSwingMotorSettings(), &c->GetTwistMotorSettings() }) {
            motor->mSpringSettings.mStiffness = stiffness;
            motor->mSpringSettings.mDamping = damping;
            motor->SetTorqueLimit(limit);
        }

        float mid = 0.5f * (joint.twist_min + joint.twist_max), half = 0.5f * (joint.twist_max - joint.twist_min);
        float twist = mid + value(2) * half;
        glm::vec2 swing(value(3) * joint.swing_y, value(4) * joint.swing_z);
        float angle = glm::length(swing);
        // A rotation of `angle` about (0, y, z), then the twist about x: swing times twist
        glm::quat q_swing = angle > 1e-6f ? glm::angleAxis(angle, glm::vec3(0.f, swing.x / angle, swing.y / angle)) : glm::quat(1.f, 0.f, 0.f, 0.f);
        glm::quat q_twist = glm::angleAxis(twist, glm::vec3(1.f, 0.f, 0.f));
        glm::quat q = glm::normalize(q_swing * q_twist);
        c->SetTargetOrientationCS(toJolt(q));
        JPH::Quat applied = c->GetTargetOrientationCS();
        if(std::abs(glm::dot(toGlm(applied), q)) < 0.99999f) bot.target_clamps++;

        glm::vec3 omega(value(5), value(6), value(7));
        c->SetTargetAngularVelocityCS(toJolt(omega * p.max_target_angular_velocity));
    }

    if(bot.decisions < 1) { /* first decision of the episode */ }
    bot.decisions++;
    bot.window_ticks = 0;

}

// Disturbances

void DisturbancesSys::tick(double h) {

    auto& bot = reg.ctx().get<HumanBot>();
    if(!bot.built || bot.kinematic || !randomizing(bot) || !bot.params.pushing) return;
    auto& rng = reg.ctx().get<WorldRng>();
    if(rng.uniform() > bot.params.push_chance) return;

    int segment = (int) std::min(rng.uniform() * bot.bodies.size(), bot.bodies.size() - 1.0);
    float strength = bot.params.push_force * bot.params.total_mass;
    glm::vec3 force = rng.unitSphere() * strength * (float) std::cbrt(rng.uniform());
    force.y = std::abs(force.y);
    glm::vec3 torque = rng.unitSphere() * strength * (float) std::cbrt(rng.uniform());
    auto& bodies = physicsWorld(reg).bodies();
    bodies.AddImpulse(bot.bodies[segment], toJolt(force));
    bodies.AddAngularImpulse(bot.bodies[segment], toJolt(torque));

}

// BodySync

void BodySyncSys::tick(double h) {

    auto& bot = reg.ctx().get<HumanBot>();
    if(!bot.built) return;
    readState(reg, bot);
    const BodyDef& def = *bot.body;
    for(int j = 0; j < humanbot_joints; j++) {
        glm::vec3 anchor = bot.state.position[def.joints[j].child];
        if(bot.state.history_valid && h > 0.) bot.state.anchor_velocity[j] = (anchor - bot.state.anchor_previous[j]) / (float) h;
        bot.state.anchor_previous[j] = anchor;
    }
    bot.state.history_valid = true;
    bot.tick++;
    bot.window_ticks++;

    if(bot.draw) {
        for(auto [e, shape, transform] : reg.view<WorldShape, Transform>().each()) {
            glm::vec3 position = bot.state.position[shape.segment] + bot.state.rotation[shape.segment] * shape.local_position;
            glm::quat rotation = glm::normalize(bot.state.rotation[shape.segment] * shape.local_rotation);
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

// Task

void HumanBotTaskSys::tick(double h) {
    auto& bot = reg.ctx().get<HumanBot>();
    if(!bot.built || bot.recording) return;
    bot.arc_ticks++;
    if(bot.arc_ticks >= arcTicks(reg, bot)) {
        bot.arc_ticks = 0;
        closeLeg(bot, false);
        placeTarget(reg, bot);
        if(!bot.continuous_speed_mode) drawSpeed(reg, bot, false);
        openLeg(bot);
    }
}

// Termination

void TerminationSys::tick(double h) {

    auto& bot = reg.ctx().get<HumanBot>();
    if(!bot.built) return;
    bot.dying = bot.isDying();

    if(bot.recording) {
        if(bot.tick >= bot.recording_ticks) bot.truncated = true;
        return;
    }
    if(bot.dying && bot.params.die_on_fall) {
        if(++bot.dying_ticks > bot.params.max_dying_frames) {
            bot.killed = true;
            closeLeg(bot, true);
            setPark(reg, true);
            return;
        }
    } else if(bot.params.reset_on_alive) {
        bot.dying_ticks = 0;
    }
    // No direction in the slope band from here: the episode ends as at its time limit, with nothing lost or won
    if(bot.tick >= episodeTicks(reg, bot) || bot.route_lost) bot.truncated = true;
    if(bot.truncated) closeLeg(bot, false);

}

// Rewards

void RewardsSys::tick(double h) {

    auto& bot = reg.ctx().get<HumanBot>();
    auto& io = agentIO(reg);
    if(!bot.built) return;

    if(io.reset_requested || bot.pending_reset) {
        io.reset_requested = false;
        resetHumanBot(reg);
        return;
    }

    const auto& clock = reg.ctx().get<WorldClock>();
    const AgentParams& p = bot.params;
    const BodyDef& def = *bot.body;
    double window = std::max(bot.window_ticks, 1) * clock.dt;
    glm::vec3 spine = bot.state.position[def.spine];
    glm::vec3 root_velocity = (spine - bot.snapshot.spine) / (float) window;
    float root_speed = glm::dot(root_velocity, safeNormalize(bot.snapshot.tsv));
    float root = std::min(root_speed, bot.snapshot.target_speed) * p.root_reward;
    if(bot.snapshot.target_speed > 0.f) root /= bot.snapshot.target_speed;

    bot.dying = bot.isDying();
    float reward = bot.dying ? p.dying_reward : p.life_reward;
    if(!bot.dying) reward += root;

    io.stats.clear();
    float height = bot.currentHeight();
    float head_error = std::clamp(1.f - height / bot.est0, 0.f, 1.f);
    float root_error = glm::length(horizontal(bot.snapshot.tsv - root_velocity));
    auto stat = [&](const char* key, float value, int aggregation = 0) { io.stats.emplace_back(key, value, aggregation); };
    const int histogram = 3;
    stat("Head/Head Spring Error", head_error, histogram);
    stat("Head/Head Height", height * def.height / bot.est0);
    stat("Death/Dying Reward", bot.dying ? p.dying_reward : p.life_reward);
    stat("Death/Alive", bot.dying ? 0.f : 1.f);
    stat("Root/Root Speed", root_speed, histogram);
    stat("Root/Root Speed Error", root_error, histogram);
    stat("Root/Root Damper Reward", root, histogram);
    stat("Root/Root Horizontal Velocity", glm::length(horizontal(root_velocity)), histogram);
    stat("Root/Root Total Velocity", glm::length(root_velocity), histogram);
    stat("Environment/Total Frame Reward", reward, histogram);
    stat(bot.dying ? "Environment/Total Dying Frame Reward" : "Environment/Total Alive Frame Reward", reward, histogram);
    stat("Collision/Step Collision Penalty", 0.f, histogram);
    stat("Collision/Collision Force", 0.f, histogram);
    stat("Collision/Collision Penalty", 0.f, histogram);
    {
        float head_speed = bot.state.linear[def.head].y;
        bot.head_max_speed = std::max(bot.head_max_speed, std::abs(head_speed));
        bot.head_max_error = std::max(bot.head_max_error, head_error);
        float head_y = bot.state.position[def.head].y;
        if(bot.ground) head_y -= bot.groundUnder(bot.state.position[def.head]);
        bot.head_max_height = std::max(bot.head_max_height, head_y);
        bot.head_min_height = std::min(bot.head_min_height, head_y);
        bot.root_max_speed = std::max(bot.root_max_speed, std::abs(root_speed));
        bot.root_max_error = std::max(bot.root_max_error, root_error);
        bot.root_max_distance = std::max(bot.root_max_distance, glm::length(horizontal(bot.routes ? spine - bot.origin : spine)));
        float spring_norm = -(root_error / std::max(bot.root_max_error, 1e-3f)) * p.root_reward;
        stat("Head/Head Damper Speed", head_speed, histogram);
        stat("Head/Head Max Speed", bot.head_max_speed);
        stat("Head/Head Max Error", bot.head_max_error);
        stat("Head/Head Max Height", bot.head_max_height);
        stat("Head/Head Min Height", bot.head_min_height);
        stat("Head/Head Spring Reward", 0.f, histogram);
        stat("Head/Head Damper Reward", 0.f, histogram);
        stat("Root/Root Max Speed", bot.root_max_speed);
        stat("Root/Root Max Error", bot.root_max_error);
        stat("Root/Root Max Distance", bot.root_max_distance);
        stat("Root/Root Spring Error", root_error, histogram);
        stat("Root/Root Spring Reward", spring_norm, histogram);
        stat("Root/Root Damper Reward Derivative", -2.f * root_error * root, histogram);
    }
    if(!bot.kinematic) {
        // Per-joint, the same names as BotJoint.OnActionReceived, averaged over the window's agents by the host
        char key[96];
        const float* a = io.action.data();
        for(int j = 0; j < humanbot_joints; j++) {
            const JointDef& joint = def.joints[j];
            const float* v = a + 8 * j;
            float spring = std::clamp((v[0] + 1.f) * 0.5f, 0.f, 1.f), damper = std::clamp((v[1] + 1.f) * 0.5f, 0.f, 1.f);
            float scale = p.torque_factor * joint.torque_factor;
            float stiffness = p.rotation_spring * spring * scale * joint.spring_factor;
            float damping = p.rotation_damper * damper * scale * joint.damper_factor;
            float action = glm::length(glm::vec3(v[2], v[3], v[4]));
            float target = glm::length(glm::vec3(v[5], v[6], v[7])) * p.max_target_angular_velocity;
            float realized = glm::length(bot.state.angular[joint.child] - bot.state.angular[joint.parent]);
            auto both = [&](const char* base, float value) {
                stat(base, value, histogram);
                std::snprintf(key, sizeof(key), "%s %s", base, joint.name.c_str());
                stat(key, value, histogram);
            };
            both("Bot Joint/Spring Factor", spring);
            both("Bot Joint/Damper Factor", damper);
            both("Bot Joint/Spring", stiffness);
            both("Bot Joint/Damper", damping);
            both("Bot Joint/Angular Velocity Factor", 0.f);
            both("Bot Joint/Target Angular Velocity", target);
            both("Bot Joint/Joint Angular Velocity", realized);
            std::snprintf(key, sizeof(key), "Bot Joint/Action %s", joint.name.c_str());
            stat(key, action, histogram);
        }
    }

    if(bot.killed) {
        reward += p.death_penalty;
        stat("Death/Death Frames", (float) bot.tick, histogram);
        stat("Death/Total Death Reward", p.death_penalty);
    } else if(bot.truncated) {
        reward += p.win_reward;
        stat("Win/Total Win Reward", p.win_reward, histogram);
    }
    if(bot.routes && bot.ground) {
        // The ground under the walker, as its path meets it: degrees up along the commanded direction and across it
        glm::vec3 along = safeNormalize(bot.snapshot.tsv);
        if(auto hit = bot.ground->below(spine + glm::vec3(0.f, 1.f, 0.f), 8.f); hit && along != glm::vec3(0.f)) {
            glm::vec3 right(-along.z, 0.f, along.x);
            stat("Terrain/Slope Along", glm::degrees(std::atan2(-glm::dot(hit->normal, along), hit->normal.y)), histogram);
            stat("Terrain/Slope Across", glm::degrees(std::abs(std::atan2(-glm::dot(hit->normal, right), hit->normal.y))), histogram);
        }
        if(bot.killed || bot.truncated) {
            stat("Terrain/Route Lost", bot.route_lost && !bot.killed ? 1.f : 0.f);
            stat("Terrain/Spawn Draws", (float) bot.spawn_tries);
        }
    }
    if(bot.killed || bot.truncated) {
        stat("Win/Win Reward", bot.killed ? 0.f : p.win_reward);
        stat("Death/Death Reward", bot.killed ? p.death_penalty : 0.f);
        stat("Win/Won", bot.killed ? 0.f : 1.f);
        stat("Death/Dead", bot.killed ? 1.f : 0.f);
    }

    // The energy reward of the window, from the exact joint work (EnergyRewardProvider, added always)
    if(p.energy_weight > 0.f || bot.books.window_pos + bot.books.window_neg > 0.f) {
        float T = (float) window;
        float positive = bot.books.window_pos / T, negative = bot.books.window_neg / T;
        float torque_rate_power = p.torque_rate_weight * bot.books.window_torque_variation / T;
        float energy = p.energy_positive_multiplier * positive + p.energy_negative_multiplier * negative + torque_rate_power;
        glm::vec3 to_target = safeNormalize(horizontal(bot.target - bot.snapshot.spine));
        float v_raw = glm::dot(horizontal(spine - bot.snapshot.spine) / T, to_target);
        float alpha = 1.f - std::exp(-T / std::max(p.energy_tau, 1e-3f));
        bot.energy_speed = bot.energy_speed_valid ? bot.energy_speed + alpha * (v_raw - bot.energy_speed) : v_raw;
        bot.energy_speed_valid = true;
        float denominator = std::clamp(bot.energy_speed, p.energy_min_speed_floor, std::max(p.energy_min_speed_floor, bot.target_speed));
        float cot = energy / denominator;
        float err = bot.energy_speed - bot.target_speed;
        float gate = std::exp(-err * err / (2.f * p.energy_speed_edge * p.energy_speed_edge));
        float r_energy = gate * std::exp(-cot / p.energy_scale) * p.energy_weight;
        reward += r_energy;
        io.stats.emplace_back("EnergyProvider/Energy Reward", r_energy, 0);
        io.stats.emplace_back("EnergyProvider/Positive Energy", positive, 0);
        io.stats.emplace_back("EnergyProvider/Negative Energy", negative, 0);
        io.stats.emplace_back("EnergyProvider/Cost of Transport", cot, 0);
        io.stats.emplace_back("EnergyProvider/Smoothed Speed", bot.energy_speed, 0);
        if(p.torque_rate_weight > 0.f) io.stats.emplace_back("EnergyProvider/Torque Rate Power", torque_rate_power, 0);
        if(bot.books.cum_abs_work > 0.) io.stats.emplace_back("Energy/Closure Residual", (float) (bot.books.cum_residual / bot.books.cum_abs_work), 0);
    }
    bot.books.window_pos = bot.books.window_neg = 0.f;
    bot.books.window_torque_variation = 0.f;

    io.reward = reward * p.reward_factor;
    bot.cumulative_reward += io.reward;

}

// Observation

void ObservationSys::tick(double h) {

    auto& bot = reg.ctx().get<HumanBot>();
    auto& io = agentIO(reg);
    if(!bot.built) return;

    // The command of the next window; the observation reads the same one
    glm::vec3 spine = bot.state.position[bot.body->spine];
    bot.snapshot.spine = spine;
    bot.snapshot.target_speed = bot.target_speed;
    bot.snapshot.tsv = safeNormalize(horizontal(bot.target - spine)) * bot.target_speed;

    std::array<float, humanbot_frame> current;
    observeV2(bot, current.data());
    for(float& f : current) if(!std::isfinite(f)) f = 0.f;
    std::copy(bot.previous_frame.begin(), bot.previous_frame.end(), io.observation.begin());
    std::copy(current.begin(), current.end(), io.observation.begin() + humanbot_frame);
    bot.previous_frame = current;

}

// Episode

void EpisodeSys::tick(double h) {

    auto& bot = reg.ctx().get<HumanBot>();
    auto& io = agentIO(reg);
    if(!bot.built) return;

    if(bot.fresh) {
        bot.fresh = false;
        io.done = false;
        io.truncated = false;
        return;
    }
    io.done = bot.killed || bot.truncated;
    io.truncated = bot.truncated && !bot.killed;
    if(io.done) {
        bot.pending_reset = true;
        setPark(reg, true);
    }

}

// TrackActions

void TrackActionsSys::init() {
    auto* bot = reg.ctx().find<HumanBot>();
    for(auto [e, player] : reg.view<PoseTrackPlayer>().each()) {
        player.data = loadTrack(resourceFile(player.track));
        if(bot && player.data->body != bot->body->name) throw AssetError("track " + player.track + " is for body " + player.data->body);
        const double clock = reg.ctx().get<WorldClock>().dt;
        if(std::abs(player.data->dt - clock) / clock > 1e-4) throw AssetError("track " + player.track + ": dt differs from the world's");
        if(!bot) continue;
        auto& bodies = physicsWorld(reg).bodies();
        for(int i = 0; i < player.data->segments; i++) {
            bodies.SetPositionAndRotation(bot->bodies[i], JPH::RVec3(toJolt(player.data->position(0, i))), toJolt(glm::normalize(player.data->rotation(0, i))), JPH::EActivation::Activate);
        }
    }
}

void TrackActionsSys::tick(double h) {
    auto& bot = reg.ctx().get<HumanBot>();
    if(!bot.built) return;
    auto& bodies = physicsWorld(reg).bodies();
    for(auto [e, player] : reg.view<PoseTrackPlayer>().each()) {
        if(!player.data || !player.playing) continue;
        int next = player.frame + 1;
        if(next >= player.data->frames) next = player.loop ? 0 : player.data->frames - 1;
        for(int i = 0; i < player.data->segments; i++) {
            JPH::RVec3 p(toJolt(player.data->position(next, i)));
            JPH::Quat q = toJolt(glm::normalize(player.data->rotation(next, i)));
            if(player.teleport || (next == 0 && player.loop)) bodies.SetPositionAndRotation(bot.bodies[i], p, q, JPH::EActivation::Activate);
            else bodies.MoveKinematic(bot.bodies[i], p, q, (float) h);
        }
        player.frame = next;
    }
}

void dy::registerHumanBot() {

    registerComponent("HumanBotSpawn", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            HumanBotSpawn s;
            JsonFields f(j);
            std::string motion = "dynamic";
            f.read("body", s.body);
            f.read("params", s.params);
            f.read("motion", motion);
            f.read("recording", s.recording);
            f.read("draw", s.draw);
            f.read("max_recording_ticks", s.max_recording_ticks);
            f.read("view", s.view);
            f.read("view_speed", s.view_speed);
            f.read("view_truncate", s.view_truncate);
            f.finish();
            if(s.body.empty()) throw ComponentError("/body", "a body file is required");
            if(s.params.empty()) throw ComponentError("/params", "a params file is required");
            if(motion != "dynamic" && motion != "kinematic") throw ComponentError("/motion", "dynamic or kinematic");
            s.kinematic = motion == "kinematic";
            reg.emplace_or_replace<HumanBotSpawn>(e, s);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& s = reg.get<HumanBotSpawn>(e);
            return nlohmann::json { { "body", s.body }, { "params", s.params }, { "motion", s.kinematic ? "kinematic" : "dynamic" }, { "recording", s.recording }, { "draw", s.draw }, { "max_recording_ticks", s.max_recording_ticks }, { "view", s.view }, { "view_speed", s.view_speed }, { "view_truncate", s.view_truncate } };
        }
    });
    registerComponent("PoseTrackPlayer", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            PoseTrackPlayer p;
            JsonFields f(j);
            f.read("track", p.track);
            f.read("loop", p.loop);
            f.read("teleport", p.teleport);
            f.finish();
            if(p.track.empty()) throw ComponentError("/track", "a track file is required");
            reg.emplace_or_replace<PoseTrackPlayer>(e, p);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& p = reg.get<PoseTrackPlayer>(e);
            return nlohmann::json { { "track", p.track }, { "loop", p.loop }, { "teleport", p.teleport } };
        }
    });

    registerSystem("AgentFactory", systemInfo<AgentFactorySys>(SystemScope::World));
    registerSystem("DriveTargets", systemInfo<DriveTargetsSys>(SystemScope::World));
    registerSystem("Disturbances", systemInfo<DisturbancesSys>(SystemScope::World));
    registerSystem("BodySync", systemInfo<BodySyncSys>(SystemScope::World));
    registerSystem("Task", systemInfo<HumanBotTaskSys>(SystemScope::World));
    registerSystem("Termination", systemInfo<TerminationSys>(SystemScope::World));
    registerSystem("Rewards", systemInfo<RewardsSys>(SystemScope::World));
    registerSystem("Observation", systemInfo<ObservationSys>(SystemScope::World));
    registerSystem("Episode", systemInfo<EpisodeSys>(SystemScope::World));
    registerSystem("TrackActions", systemInfo<TrackActionsSys>(SystemScope::World));
    registerHumanBotEnergy();

}
