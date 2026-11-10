#include "humanbot.h"

#include <Jolt/Physics/Body/BodyLock.h>

#include <algorithm>
#include <cmath>

#include "physics/physics_world.h"


using namespace dy;

// The three energy formulas of a long rollout (docs/physical-agents/energy-formulas.md). Read only: nothing here feeds
// the reward or the physics

namespace {

// Jolt's solved contact impulses of the step just done (patch P-C). Training worlds step on one thread
class ContactRecorder : public JPH::ContactImpulseListener {
public:
    void OnContactSolved(const JPH::ContactImpulseReport& report) override { reports.push_back(report); }
    std::vector<JPH::ContactImpulseReport> reports;
};

struct ContactRecorderHandle { std::shared_ptr<ContactRecorder> recorder; };

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

glm::vec3 toGlmV(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }

// The joint's anchor carried by one segment: Jolt's two local anchors come from the same world point at the rest pose
glm::vec3 anchorOn(const SegmentDef& segment, glm::vec3 anchor, glm::vec3 position, glm::quat rotation) {
    return position + rotation * (glm::inverse(segment.rest_rotation) * (anchor - segment.rest_position));
}

int segmentOf(const HumanBot& bot, JPH::BodyID id) {
    for(int i = 0; i < (int) bot.bodies.size(); i++) if(bot.bodies[i] == id) return i;
    return -1;
}

}

FormulaTotals& FormulaTotals::operator+=(const FormulaTotals& o) {
    ticks += o.ticks; stances += o.stances; seconds += o.seconds; metres += o.metres;
    lagrangian_abs += o.lagrangian_abs; lagrangian += o.lagrangian; body_energy += o.body_energy;
    rne += o.rne; drive += o.drive; rne_sagittal += o.rne_sagittal; drive_sagittal += o.drive_sagittal;
    rne_stance_3d += o.rne_stance_3d; drive_stance_3d += o.drive_stance_3d;
    point_error += o.point_error; point_reference += o.point_reference;
    torque_error_free += o.torque_error_free; torque_reference_free += o.torque_reference_free; limit_torque += o.limit_torque;
    joint_ticks_free += o.joint_ticks_free; joint_ticks_limit += o.joint_ticks_limit;
    contact_error += o.contact_error; contact_reference += o.contact_reference;
    root_linear += o.root_linear; root_angular += o.root_angular; weight_impulse += o.weight_impulse;
    closure_net += o.closure_net; closure_tickwise += o.closure_tickwise; closure_gross += o.closure_gross;
    closure_point_net += o.closure_point_net; closure_point_tickwise += o.closure_point_tickwise;
    contact_work += o.contact_work; clamped_ticks += o.clamped_ticks; torque_variation += o.torque_variation;
    return *this;
}

void dy::EnergyFormulasSys::tick(double h) {

    auto& bot = reg.ctx().get<HumanBot>();
    if(!bot.built || bot.kinematic || !bot.view_truncate) return;
    auto& physics = physicsWorld(reg);

    auto* handle = reg.ctx().find<ContactRecorderHandle>();
    if(!handle) {
        auto recorder = std::make_shared<ContactRecorder>();
        physics.system().SetContactImpulseListener(recorder.get());
        handle = &reg.ctx().emplace<ContactRecorderHandle>(ContactRecorderHandle { recorder });
    }
    auto& reports = handle->recorder->reports;

    FormulaBooks& f = bot.formulas;
    FormulaTotals& t = f.totals;
    const BodyDef& def = *bot.body;
    const auto& s = bot.state;
    const float dt = (float) h;
    const glm::vec3 gravity = physics.config().gravity;
    const int segments = 16;

    // Formula 1, at the end of this step: L = sum (T - V), and Unity's body energy T + V
    double lagrangian = 0., energy = 0.;
    for(int i = 0; i < segments; i++) {
        float m = massOf(bot, i);
        glm::vec3 com = s.position[i] + s.rotation[i] * def.segments[i].center_of_mass;
        glm::mat3 inertia = inertiaWorld(bot, i, s.rotation[i]);
        double T = 0.5 * m * glm::dot(s.linear[i], s.linear[i]) + 0.5 * glm::dot(s.angular[i], inertia * s.angular[i]);
        double V = -m * glm::dot(gravity, com);
        lagrangian += T - V;
        energy += T + V;
    }

    if(f.deepest_first.empty()) {
        std::vector<int> depth(def.joints.size());
        for(size_t j = 0; j < def.joints.size(); j++) {
            for(int p = def.segments[def.joints[j].child].parent; p >= 0; p = def.segments[p].parent) depth[j]++;
            f.deepest_first.push_back((int) j);
        }
        std::stable_sort(f.deepest_first.begin(), f.deepest_first.end(), [&](int a, int b) { return depth[a] > depth[b]; });
    }

    if(f.valid) {

        const bool settled = bot.tick * dt >= bot.params.stats_settle_seconds;

        std::array<glm::vec3, 16> com0, velocity_mid, omega_mid;
        for(int i = 0; i < segments; i++) {
            com0[i] = f.prev_position[i] + f.prev_rotation[i] * def.segments[i].center_of_mass;
            velocity_mid[i] = 0.5f * (f.prev_linear[i] + s.linear[i]);
            omega_mid[i] = 0.5f * (f.prev_angular[i] + s.angular[i]);
        }

        // Ground impulse and friction as Jolt solved them: normal at each point, linear friction at the friction
        // centre, angular friction about the normal. On body 2 with +, on body 1 with -
        std::array<glm::vec3, 16> J_ext, L_ext;
        std::array<float, 16> normal_impulse;
        std::array<bool, 16> touched;
        J_ext.fill(glm::vec3(0.f));
        L_ext.fill(glm::vec3(0.f));
        normal_impulse.fill(0.f);
        touched.fill(false);
        double contact_work = 0.;
        for(const auto& r : reports) {
            glm::vec3 normal = toGlmV(r.mNormal);
            glm::vec3 friction = r.mFrictionLambda[0] * toGlmV(r.mTangent1) + r.mFrictionLambda[1] * toGlmV(r.mTangent2);
            glm::vec3 friction_point = toGlmV(JPH::Vec3(r.mFrictionPoint));
            const int ids[2] = { segmentOf(bot, r.mBody1), segmentOf(bot, r.mBody2) };
            for(int side = 0; side < 2; side++) {
                int i = ids[side];
                if(i < 0) continue;
                float sign = side == 1 ? 1.f : -1.f;
                touched[i] = true;
                for(uint32_t p = 0; p < r.mNumPoints; p++) {
                    glm::vec3 point = toGlmV(JPH::Vec3(r.mPoints[p]));
                    glm::vec3 J = sign * r.mNormalLambda[p] * normal;
                    J_ext[i] += J;
                    L_ext[i] += glm::cross(point - com0[i], J);
                    normal_impulse[i] += r.mNormalLambda[p];
                    contact_work += glm::dot(J, velocity_mid[i] + glm::cross(omega_mid[i], point - com0[i]));
                }
                glm::vec3 J = sign * friction;
                J_ext[i] += J;
                L_ext[i] += glm::cross(friction_point - com0[i], J);
                glm::vec3 torsion = sign * r.mAngularFrictionLambda * normal;
                L_ext[i] += torsion;
                contact_work += glm::dot(J, velocity_mid[i] + glm::cross(omega_mid[i], friction_point - com0[i])) + glm::dot(torsion, omega_mid[i]);
            }
        }

        // Formula 2: recursive Newton-Euler, leaf to root, from the measured kinematics and the ground impulse only.
        // Written on impulses over the step and on the start of step pose, as Jolt integrates: I at the start rotation
        // and no gyroscopic term (Jolt applies none here), so that nothing the simulator did not do is invented
        std::array<glm::vec3, 16> J_children, L_children;
        J_children.fill(glm::vec3(0.f));
        L_children.fill(glm::vec3(0.f));
        std::array<glm::vec3, humanbot_joints> J_joint, L_joint, omega_rel;
        auto netLinear = [&](int i) { float m = massOf(bot, i); return m * (s.linear[i] - f.prev_linear[i]) - m * gravity * dt; };
        auto netAngular = [&](int i) { return inertiaWorld(bot, i, f.prev_rotation[i]) * (s.angular[i] - f.prev_angular[i]); };
        for(int j : f.deepest_first) {
            const JointDef& joint = def.joints[j];
            int c = joint.child, p = joint.parent;
            glm::vec3 J = netLinear(c) + J_children[c] - J_ext[c];
            glm::vec3 arm_c = anchorOn(def.segments[c], joint.anchor, f.prev_position[c], f.prev_rotation[c]) - com0[c];
            glm::vec3 L = netAngular(c) - glm::cross(arm_c, J) + L_children[c] - L_ext[c];
            J_joint[j] = J;
            L_joint[j] = L;
            glm::vec3 arm_p = anchorOn(def.segments[p], joint.anchor, f.prev_position[p], f.prev_rotation[p]) - com0[p];
            J_children[p] += J;
            L_children[p] += L + glm::cross(arm_p, J);
            omega_rel[j] = omega_mid[c] - omega_mid[p];
        }

        // Formula 3 and the comparison of 2 with what Jolt applied
        std::array<float, humanbot_joints> work_rne, work_drive;
        std::array<glm::vec3, humanbot_joints> L_motor;
        std::array<glm::vec3, 16> J_point_sum;
        J_point_sum.fill(glm::vec3(0.f));
        double joint_net = 0., point_work = 0.;
        for(int j = 0; j < humanbot_joints; j++) {
            const JointDef& joint = def.joints[j];
            auto* c = bot.constraints[j].GetPtr();
            glm::quat frame = glm::normalize(f.prev_rotation[joint.child] * toGlm(c->GetConstraintToBody2()));
            JPH::Vec3 lambda = c->GetTotalLambdaMotor();
            L_motor[j] = lambda.GetX() * (frame * glm::vec3(1, 0, 0)) + lambda.GetY() * (frame * glm::vec3(0, 1, 0)) + lambda.GetZ() * (frame * glm::vec3(0, 0, 1));
            work_rne[j] = glm::dot(L_joint[j], omega_rel[j]);
            work_drive[j] = glm::dot(L_motor[j], omega_rel[j]);
            joint_net += work_rne[j];

            glm::vec3 point = toGlmV(c->GetTotalLambdaPosition());
            J_point_sum[joint.child] += point;
            J_point_sum[joint.parent] -= point;
            glm::vec3 r_c = anchorOn(def.segments[joint.child], joint.anchor, f.prev_position[joint.child], f.prev_rotation[joint.child]) - com0[joint.child];
            glm::vec3 r_p = anchorOn(def.segments[joint.parent], joint.anchor, f.prev_position[joint.parent], f.prev_rotation[joint.parent]) - com0[joint.parent];
            point_work += glm::dot(point, velocity_mid[joint.child] + glm::cross(omega_mid[joint.child], r_c) - velocity_mid[joint.parent] - glm::cross(omega_mid[joint.parent], r_p));
            t.point_error += glm::length(J_joint[j] - point);
            t.point_reference += glm::length(point);
            float difference = glm::length(L_joint[j] - L_motor[j]);
            bool limit = c->GetTotalLambdaTwist() != 0.f || c->GetTotalLambdaSwingY() != 0.f || c->GetTotalLambdaSwingZ() != 0.f;
            if(limit) {
                t.limit_torque += difference;
                t.joint_ticks_limit++;
            } else {
                t.torque_error_free += difference;
                t.torque_reference_free += glm::length(L_motor[j]);
                t.joint_ticks_free++;
            }
        }

        // What the recursion leaves on the root, and Jolt's contact impulse against the momentum balance
        int root = -1;
        for(int i = 0; i < segments; i++) if(def.segments[i].parent < 0) root = i;
        t.root_linear += glm::length(netLinear(root) + J_children[root] - J_ext[root]);
        t.root_angular += glm::length(netAngular(root) + L_children[root] - L_ext[root]);
        t.weight_impulse += bot.params.total_mass * glm::length(gravity) * dt;
        for(int i = 0; i < segments; i++) {
            if(!touched[i]) continue;
            t.contact_error += glm::length(J_ext[i] - (netLinear(i) - J_point_sum[i]));
            t.contact_reference += glm::length(J_ext[i]);
            for(int a = 0; a < 3; a++) if(std::abs(s.angular[i][a]) >= bot.params.max_angular_velocity * 0.999f) { t.clamped_ticks++; break; }
        }

        double dE = energy - f.prev_energy;
        double residual = dE - joint_net - contact_work;
        t.closure_net += residual;
        t.closure_tickwise += std::abs(residual);
        t.closure_gross += std::abs(joint_net) + std::abs(contact_work);
        t.closure_point_net += residual - point_work;
        t.closure_point_tickwise += std::abs(residual - point_work);
        t.contact_work += contact_work;

        // DeVita's frame and limbs (Unity's DeVitaStepStat)
        glm::vec3 up(0.f, 1.f, 0.f);
        glm::vec3 forward = safeNormalize(horizontal(bot.target - s.position[def.spine]));
        glm::vec3 ml = safeNormalize(glm::cross(up, forward));
        const float threshold = 0.05f * bot.params.total_mass * glm::length(gravity);
        const int joints[2][3] = { { bot.left_hip, bot.left_knee, bot.left_ankle }, { bot.right_hip, bot.right_knee, bot.right_ankle } };
        const int feet[2] = { def.left_foot, def.right_foot };
        for(int side = 0; side < 2; side++) {
            FormulaStanceWork tick_work;
            for(int a = 0; a < 3; a++) {
                int j = joints[side][a];
                float omega_ml = glm::dot(omega_rel[j], ml);
                tick_work.rne_sagittal.add(glm::dot(L_joint[j], ml) * omega_ml);
                tick_work.drive_sagittal.add(glm::dot(L_motor[j], ml) * omega_ml);
                tick_work.rne_3d.add(work_rne[j]);
                tick_work.drive_3d.add(work_drive[j]);
            }
            bool loaded = normal_impulse[feet[side]] / dt >= threshold;
            f.limb[side].step(loaded, bot.tick, dt, 3, 2.f,
                [&](FormulaStanceWork& w) { w += tick_work; },
                [&](const FormulaStanceWork& w, int ticks, int start) {
                    if(ticks * dt < 0.15f || start * dt < bot.params.stats_settle_seconds) return;
                    t.rne_sagittal += w.rne_sagittal;
                    t.drive_sagittal += w.drive_sagittal;
                    t.rne_stance_3d += w.rne_3d;
                    t.drive_stance_3d += w.drive_3d;
                    t.stances++;
                });
        }

        if(settled) {
            double dL = lagrangian - f.prev_lagrangian;
            t.lagrangian_abs += std::abs(dL);
            t.lagrangian.add(dL);
            t.body_energy.add(dE);
            for(int j = 0; j < humanbot_joints; j++) {
                t.rne.add(work_rne[j]);
                t.drive.add(work_drive[j]);
            }
            t.torque_variation += bot.books.tick_torque_variation;
            t.ticks++;
            t.seconds += dt;
            t.metres += glm::dot(horizontal(s.position[def.spine] - f.prev_position[def.spine]), forward);
        }
    }

    reports.clear();
    f.valid = true;
    f.prev_lagrangian = lagrangian;
    f.prev_energy = energy;
    for(int i = 0; i < segments; i++) {
        f.prev_position[i] = s.position[i];
        f.prev_rotation[i] = s.rotation[i];
        f.prev_linear[i] = s.linear[i];
        f.prev_angular[i] = s.angular[i];
    }

}
