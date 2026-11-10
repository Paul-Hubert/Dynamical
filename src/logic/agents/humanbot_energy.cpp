#include "humanbot.h"

#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include <algorithm>
#include <cmath>

#include "agent_env_params.h"
#include "agent_io.h"
#include "logic/components/rigid_body.h"
#include "physics/physics_world.h"
#include "scene/system_registry.h"
#include "scene/world.h"
#include "util/log.h"

using namespace dy;

namespace {

float massOf(const HumanBot& bot, int segment) {
    return bot.params.total_mass * bot.body->segments[segment].percentage / 100.f;
}

glm::mat3 inertiaWorld(const HumanBot& bot, int segment, glm::quat rotation) {
    const SegmentDef& s = bot.body->segments[segment];
    float scale = bot.params.total_mass / bot.body->total_mass;
    glm::mat3 r = glm::mat3_cast(glm::normalize(rotation * s.inertia_rotation));
    glm::mat3 d(s.inertia.x * scale, 0.f, 0.f, 0.f, s.inertia.y * scale, 0.f, 0.f, 0.f, s.inertia.z * scale);
    return r * d * glm::transpose(r);
}

glm::vec3 comWorld(const HumanBot& bot, int segment, const glm::vec3* positions, const glm::quat* rotations) {
    return positions[segment] + rotations[segment] * bot.body->segments[segment].center_of_mass;
}

}

// The books of one physics tick, from the constraint impulses of the step just done (docs/physical-agents/physics.md).
// Whole-body energy E = KE + PE, per-joint motor work from the motor lambdas, per-segment contact impulse from the
// momentum balance (flat or sloped ground: one surface per body)
void dy::EnergySys::tick(double h) {

    auto& bot = reg.ctx().get<HumanBot>();
    if(!bot.built || bot.kinematic) return;
    EnergyBooks& e = bot.books;
    const float dt = (float) h;
    const auto& s = bot.state;
    const BodyDef& def = *bot.body;
    auto& physics = physicsWorld(reg);
    const glm::vec3 gravity = physics.config().gravity;

    // Kinetic and potential energy at the end of this step
    double energy = 0.;
    for(int i = 0; i < 16; i++) {
        float m = massOf(bot, i);
        glm::vec3 com = comWorld(bot, i, s.position.data(), s.rotation.data());
        glm::mat3 inertia = inertiaWorld(bot, i, s.rotation[i]);
        energy += 0.5f * m * glm::dot(s.linear[i], s.linear[i]) + 0.5f * glm::dot(s.angular[i], inertia * s.angular[i]) - m * glm::dot(gravity, com);
    }

    if(e.valid) {
        float dE = (float) (energy - e.energy);
        float pos = 0.f, neg = 0.f;
        float motor_total = 0.f;
        float torque_variation = 0.f;

        for(int j = 0; j < humanbot_joints; j++) {
            const JointDef& joint = def.joints[j];
            auto& w = e.joints[j];
            auto* c = bot.constraints[j].GetPtr();
            // Motor axes: the constraint frame carried by body 2 at the start of the step
            glm::quat frame = glm::normalize(e.prev_rotation[joint.child] * toGlm(c->GetConstraintToBody2()));
            glm::vec3 axes[3] = { frame * glm::vec3(1, 0, 0), frame * glm::vec3(0, 1, 0), frame * glm::vec3(0, 0, 1) };
            JPH::Vec3 lambda = c->GetTotalLambdaMotor();
            // The motor adds +lambda to body 2 and -lambda to body 1 (about the axis)
            glm::vec3 impulse = lambda.GetX() * axes[0] + lambda.GetY() * axes[1] + lambda.GetZ() * axes[2];
            glm::vec3 omega_start = e.prev_angular[joint.child] - e.prev_angular[joint.parent];
            glm::vec3 omega_end = s.angular[joint.child] - s.angular[joint.parent];
            glm::vec3 omega_mid = 0.5f * (omega_start + omega_end);
            float work = glm::dot(impulse, omega_mid);
            w.torque = impulse / dt;
            if(e.prev_torque_valid) torque_variation += glm::length(w.torque - e.prev_torque[j]);
            e.prev_torque[j] = w.torque;
            w.omega = omega_mid;
            w.work = work;
            if(work >= 0.f) pos += work; else neg -= work;
            motor_total += work;
        }

        // Contact: what remains of each segment's momentum change after gravity and the joints' linear impulses
        float contact_work = 0.f;
        for(int i = 0; i < 16; i++) {
            glm::vec3 impulse = massOf(bot, i) * (s.linear[i] - e.prev_linear[i]) - massOf(bot, i) * gravity * dt;
            for(int j = 0; j < humanbot_joints; j++) {
                const JointDef& joint = def.joints[j];
                if(joint.child != i && joint.parent != i) continue;
                glm::vec3 point = toGlm(bot.constraints[j]->GetTotalLambdaPosition());
                impulse -= joint.child == i ? point : -point;
            }
            e.contact_impulse[i] = impulse;
            contact_work += glm::dot(impulse, 0.5f * (s.linear[i] + e.prev_linear[i]));
        }

        e.prev_torque_valid = true;
        e.tick_torque_variation = torque_variation;
        e.window_torque_variation += torque_variation;
        e.tick_pos = pos;
        e.tick_neg = neg;
        e.window_pos += pos;
        e.window_neg += neg;
        e.tick_motor = motor_total;
        e.tick_contact = contact_work;
        e.tick_energy = dE;
        e.tick_residual = dE - motor_total - contact_work;
        e.cum_abs_work += std::abs(motor_total) + std::abs(contact_work);
        e.cum_residual += std::abs(e.tick_residual);
        e.cum_net_residual += e.tick_residual;
    } else {
        for(auto& w : e.joints) w = JointWork {};
        e.prev_torque_valid = false;
        e.tick_torque_variation = 0.f;
        e.contact_impulse.fill(glm::vec3(0.f));
    }

    e.energy = energy;
    e.valid = true;
    for(int i = 0; i < 16; i++) {
        e.prev_linear[i] = s.linear[i];
        e.prev_angular[i] = s.angular[i];
        e.prev_rotation[i] = s.rotation[i];
    }

}

void dy::DeVitaStepStatSys::tick(double h) {

    auto& bot = reg.ctx().get<HumanBot>();
    if(!bot.built || bot.kinematic || !bot.books.valid || (bot.params.energy_weight <= 0.f && !bot.view_truncate)) return;  // eval measures every policy
    const auto& e = bot.books;
    DeVitaBooks& d = bot.devita;
    const auto& def = *bot.body;
    const float dt = (float) h;
    float threshold = 0.05f * bot.params.total_mass * 9.81f;   // N: the stance load, 0.05 m g

    glm::vec3 spine = bot.state.position[def.spine];
    glm::vec3 up(0.f, 1.f, 0.f);
    glm::vec3 forward = safeNormalize(horizontal(bot.target - spine));
    glm::vec3 ml = safeNormalize(glm::cross(up, forward));
    int joints[2][3] = { { bot.left_hip, bot.left_knee, bot.left_ankle }, { bot.right_hip, bot.right_knee, bot.right_ankle } };
    int feet[2] = { def.left_foot, def.right_foot };

    for(int side = 0; side < 2; side++) {
        float normal = e.contact_impulse[feet[side]].y / dt;
        bool loaded = normal >= threshold;

        // Work of the limb's joints this tick, split per joint by sign, on the sagittal axis
        float pos = 0.f, neg = 0.f;
        for(int a = 0; a < 3; a++) {
            const JointWork& w = e.joints[joints[side][a]];
            float w_s = glm::dot(w.torque, ml) * glm::dot(w.omega, ml) * dt;
            if(w_s >= 0.f) pos += w_s; else neg -= w_s;
        }
        const bool was = d.stance[side].in_stance;
        d.stance[side].step(loaded, bot.tick, dt, 3, 2.f,
            [&](SagittalWork& w) { w.pos += pos; w.neg += neg; },
            [&](const SagittalWork& w, int ticks, int start) {
                if(ticks * dt < 0.15f || start * dt < bot.params.stats_settle_seconds) return;
                d.sum_pos += w.pos;
                d.sum_neg += w.neg;
                d.stances++;
            });
        if(!was && d.stance[side].in_stance) d.strikes++;
    }

}

void dy::applySlope(entt::registry& reg, HumanBot& bot) {

    auto* env = reg.ctx().find<AgentEnvParams>();
    float amplitude = env ? env->get("slopeAngle", bot.slope_angle) : bot.slope_angle;
    bool random = env && env->get("slopeRandom", 0.f) > 0.f;
    bool level = env && env->get("slopeIncludeLevel", 0.f) > 0.f;
    float sign = env ? env->get("slopeSign", 0.f) : 0.f;
    bool compensate = env && env->get("slopeCompensateSpeed", 1.f) > 0.f;
    float reach = env ? env->get("slopeFootReach", 0.f) : 0.f;
    float margin = env ? env->get("slopeLiftMargin", 0.01f) : 0.01f;
    float degrees = 0.f;
    if(!random) {
        degrees = std::abs(sign) > 0.5f ? (sign > 0.f ? amplitude : -amplitude) : 0.f;
    } else {
        auto& rng = reg.ctx().get<WorldRng>();
        int k = (int) (rng.uniform() * (level ? 3 : 2));
        degrees = level ? (k == 0 ? amplitude : k == 1 ? 0.f : -amplitude) : (k == 0 ? amplitude : -amplitude);
    }
    if(reach <= 0.f) reach = 0.25f;
    bot.slope_applied = degrees;

    auto& bodies = physicsWorld(reg).bodies();
    for(auto [e, ground] : reg.view<Ground>().each()) {
        if(!ground.created()) continue;
        glm::vec3 heading(0.f, 0.f, -1.f);
        float rad = glm::radians(degrees);
        glm::vec3 desired = glm::normalize(heading * std::cos(rad) + glm::vec3(0.f, 1.f, 0.f) * std::sin(rad));
        glm::vec3 axis = glm::cross(heading, desired);
        float length = glm::length(axis);
        glm::quat tilt = length > 1e-6f ? glm::angleAxis(std::asin(std::min(length, 1.f)), axis / length) : glm::quat(1.f, 0.f, 0.f, 0.f);
        float drop = reach * std::tan(std::abs(rad)) + (degrees != 0.f ? margin : 0.f);
        glm::vec3 center(0.f, ground.top_y - ground.size.y * 0.5f - drop, 0.f);
        // The box turns about its top face's centre
        glm::vec3 top(0.f, ground.top_y - drop, 0.f);
        glm::vec3 position = top + tilt * (center - top);
        bodies.SetPositionAndRotation(JPH::BodyID(ground.body), JPH::RVec3(toJolt(position)), toJolt(tilt), JPH::EActivation::DontActivate);
    }
    if(compensate && degrees != 0.f) bot.target_speed *= std::cos(glm::radians(degrees));

}

void dy::registerHumanBotEnergy() {
    registerSystem("Energy", systemInfo<EnergySys>(SystemScope::World));
    registerSystem("DeVitaStepStat", systemInfo<DeVitaStepStatSys>(SystemScope::World));
    registerSystem("EnergyFormulas", systemInfo<EnergyFormulasSys>(SystemScope::World));
}
