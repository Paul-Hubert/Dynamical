#include "ik.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "humanbot.h"
#include "physics/physics_sys.h"
#include "physics/physics_world.h"
#include "scene/system_registry.h"
#include "util/log.h"

using namespace dy;

namespace {

glm::vec3 direction(glm::vec3 v, glm::vec3 fallback) {
    float l = glm::length(v);
    return l > 1e-8f ? v / l : fallback;
}

// The shortest rotation taking unit a to unit b
glm::quat fromTo(glm::vec3 a, glm::vec3 b) {
    float d = glm::dot(a, b);
    if(d > 0.999999f) return glm::quat(1.f, 0.f, 0.f, 0.f);
    if(d < -0.999999f) {
        glm::vec3 axis = glm::cross(a, std::abs(a.x) < 0.9f ? glm::vec3(1.f, 0.f, 0.f) : glm::vec3(0.f, 1.f, 0.f));
        return glm::angleAxis(3.14159265358979f, glm::normalize(axis));
    }
    glm::vec3 c = glm::cross(a, b);
    return glm::normalize(glm::quat(1.f + d, c.x, c.y, c.z));
}

// Orthonormal frame with x along d and y as close to n as it can be
bool basis(glm::vec3 d, glm::vec3 n, glm::quat& out) {
    glm::vec3 y = n - d * glm::dot(n, d);
    float l = glm::length(y);
    if(l < 1e-5f) return false;
    y /= l;
    out = glm::normalize(glm::quat_cast(glm::mat3(d, y, glm::cross(d, y))));
    return true;
}

float angleBetween(glm::quat a, glm::quat b) {
    return 2.f * std::acos(std::min(1.f, std::abs(glm::dot(a, b))));
}

}

FabrikResult dy::fabrik(std::vector<glm::vec3>& p, const std::vector<float>& lengths, glm::vec3 target, const glm::vec3* pole,
    int max_iterations, float tolerance) {

    FabrikResult result;
    const int n = (int) p.size();
    if(n < 2) return result;
    const glm::vec3 root = p[0];

    float total = 0.f;
    for(float l : lengths) total += l;
    glm::vec3 to_target = target - root;
    float distance = glm::length(to_target);

    if(distance >= total) {
        // Out of reach: straight at the target
        glm::vec3 dir = direction(to_target, glm::vec3(0.f, -1.f, 0.f));
        for(int i = 0; i + 1 < n; i++) p[i + 1] = p[i] + dir * lengths[i];
        result.error = distance - total;
        return result;
    }

    // A straight chain aimed at the target would stay straight: bend it a little, toward the pole when there is one
    {
        glm::vec3 axis = direction(to_target, glm::vec3(0.f, -1.f, 0.f));
        glm::vec3 side = pole ? *pole - axis * glm::dot(*pole, axis) : glm::vec3(0.f);
        if(glm::length(side) < 1e-6f) side = glm::cross(axis, std::abs(axis.x) < 0.9f ? glm::vec3(1.f, 0.f, 0.f) : glm::vec3(0.f, 1.f, 0.f));
        side = glm::normalize(side);
        for(int i = 1; i + 1 < n; i++) {
            glm::vec3 a = p[i] - root;
            glm::vec3 off = a - axis * glm::dot(a, axis);
            if(glm::length(off) < 1e-3f * total) p[i] += side * (1e-2f * total);
        }
    }

    result.error = glm::length(p[n - 1] - target);
    while(result.iterations < max_iterations && result.error > tolerance) {
        p[n - 1] = target;
        for(int i = n - 2; i >= 0; i--) p[i] = p[i + 1] + direction(p[i] - p[i + 1], glm::vec3(0.f, 1.f, 0.f)) * lengths[i];
        p[0] = root;
        for(int i = 0; i + 1 < n; i++) p[i + 1] = p[i] + direction(p[i + 1] - p[i], glm::vec3(0.f, -1.f, 0.f)) * lengths[i];
        result.error = glm::length(p[n - 1] - target);
        result.iterations++;
    }

    if(pole) {
        // Each inner point turns about the line through its neighbours until it is on the pole's side
        for(int i = 1; i + 1 < n; i++) {
            glm::vec3 axis = p[i + 1] - p[i - 1];
            float l = glm::length(axis);
            if(l < 1e-8f) continue;
            axis /= l;
            glm::vec3 a = p[i] - p[i - 1];
            float along = glm::dot(a, axis);
            glm::vec3 off = a - axis * along;
            glm::vec3 side = *pole - axis * glm::dot(*pole, axis);
            float radius = glm::length(off), s = glm::length(side);
            if(radius < 1e-8f || s < 1e-6f) continue;
            p[i] = p[i - 1] + axis * along + side * (radius / s);
        }
    }

    return result;

}

IKSolve dy::solveChain(const RigModel& model, RigPose& pose, const std::vector<int>& joints, glm::vec3 pole_rest,
    const IKGoal& goal, bool limits) {

    IKSolve solve;
    const BodyDef& def = *model.body;
    const int n = (int) joints.size();
    if(n == 0) return solve;

    SegmentPoses poses;
    model.forward(pose, poses);

    std::vector<glm::vec3> points(n), rest(n);
    std::vector<float> lengths(std::max(0, n - 1));
    for(int i = 0; i < n; i++) {
        points[i] = model.anchor(poses, joints[i]);
        rest[i] = def.joints[joints[i]].anchor;
    }
    for(int i = 0; i + 1 < n; i++) lengths[i] = glm::length(rest[i + 1] - rest[i]);

    const int root_parent = def.joints[joints[0]].parent;
    const bool has_pole = glm::length(pole_rest) > 1e-6f;
    glm::vec3 pole = goal.pole;
    if(has_pole && glm::length(pole) < 1e-6f) {
        pole = poses.rotation[root_parent] * (glm::inverse(def.segments[root_parent].rest_rotation) * pole_rest);
    }

    if(n >= 2) {
        FabrikResult r = fabrik(points, lengths, goal.position, has_pole ? &pole : nullptr);
        solve.iterations = r.iterations;
    }

    // Bone rotations, root first: each joint takes what its limits allow, the next one sees the result
    glm::quat parent = poses.rotation[root_parent];
    glm::quat plane_rest(1.f, 0.f, 0.f, 0.f), plane(1.f, 0.f, 0.f, 0.f);
    glm::vec3 normal_rest(0.f), normal(0.f);
    bool planar = false;
    if(has_pole && n >= 3) {
        glm::vec3 line_rest = direction(rest[n - 1] - rest[0], glm::vec3(0.f, -1.f, 0.f));
        glm::vec3 line = direction(points[n - 1] - points[0], glm::vec3(0.f, -1.f, 0.f));
        normal_rest = glm::cross(line_rest, pole_rest);
        normal = glm::cross(line, pole);
        planar = glm::length(normal_rest) > 1e-5f && glm::length(normal) > 1e-5f;
        if(planar) {
            normal_rest = glm::normalize(normal_rest);
            normal = glm::normalize(normal);
        }
    }

    for(int i = 0; i + 1 < n; i++) {
        const int j = joints[i];
        const int segment = def.joints[j].child;
        glm::vec3 d = direction(points[i + 1] - points[i], glm::vec3(0.f, -1.f, 0.f));
        glm::vec3 d_rest = direction(rest[i + 1] - rest[i], glm::vec3(0.f, -1.f, 0.f));
        glm::quat world;
        if(planar && basis(d, normal, plane) && basis(d_rest, normal_rest, plane_rest)) {
            world = glm::normalize(plane * glm::inverse(plane_rest) * def.segments[segment].rest_rotation);
        } else {
            // No plane to hold the roll: the shortest turn from where the bone is
            glm::quat current = model.childRotation(j, parent, pose.joint[j]);
            glm::vec3 now = current * (glm::inverse(def.segments[segment].rest_rotation) * d_rest);
            world = glm::normalize(fromTo(now, d) * current);
        }
        glm::quat q = model.jointRotation(j, parent, world);
        if(limits) {
            float excess = 0.f;
            q = model.clamp(j, q, &excess);
            solve.limit_excess = std::max(solve.limit_excess, excess);
        }
        pose.joint[j] = q;
        parent = model.childRotation(j, parent, q);
    }

    const int tip = joints[n - 1];
    if(goal.use_rotation) {
        glm::quat q = model.jointRotation(tip, parent, goal.rotation);
        if(limits) {
            float excess = 0.f;
            q = model.clamp(tip, q, &excess);
            solve.limit_excess = std::max(solve.limit_excess, excess);
        }
        pose.joint[tip] = q;
    }

    model.forward(pose, poses);
    solve.position_error = glm::length(model.anchor(poses, tip) - goal.position);
    if(goal.use_rotation) solve.rotation_error = angleBetween(poses.rotation[def.joints[tip].child], goal.rotation);
    return solve;

}

void FabrikSys::tick(double h) {

    OPTICK_EVENT();

    ensureRig(reg);

    std::map<entt::entity, std::vector<const IKTarget*>> wanted;
    for(auto [e, target] : reg.view<IKTarget>().each()) {
        if(!target.enabled || target.weight <= 0.f || !reg.valid(target.chain)) continue;
        wanted[target.chain].push_back(&target);
    }

    for(auto [e, chain] : reg.view<IKChain>().each()) {
        chain.solved = false;
        auto it = wanted.find(e);
        if(it == wanted.end() || !reg.valid(chain.rig)) continue;
        auto* rig = reg.try_get<Rig>(chain.rig);
        auto* pose = reg.try_get<RigPose>(chain.rig);
        if(!rig || !pose) continue;

        int priority = it->second[0]->priority;
        for(auto* t : it->second) priority = std::max(priority, t->priority);

        IKGoal goal;
        goal.position = glm::vec3(0.f);
        glm::quat rotation(0.f, 0.f, 0.f, 0.f);
        const IKTarget* first = nullptr;
        float total = 0.f, rotation_weight = 0.f;
        for(auto* t : it->second) {
            if(t->priority != priority) continue;
            if(!first) first = t;
            total += t->weight;
            goal.position += t->position * t->weight;
            goal.pole += t->pole * t->weight;
            if(t->use_rotation) {
                glm::quat q = glm::dot(t->rotation, first->rotation) < 0.f ? -t->rotation : t->rotation;
                rotation += q * t->weight;
                rotation_weight += t->weight;
            }
        }
        goal.position /= total;
        goal.use_rotation = rotation_weight > 0.f;
        if(goal.use_rotation) goal.rotation = glm::normalize(rotation);
        if(glm::length(goal.pole) > 1e-6f) goal.pole = glm::normalize(goal.pole);

        std::vector<glm::quat> before;
        if(total < 1.f) {
            for(int j : chain.joints) before.push_back(pose->joint[j]);
        }
        IKSolve solve = solveChain(*rig->model, *pose, chain.joints, chain.pole_rest, goal, chain.limits);
        if(total < 1.f) {
            for(size_t i = 0; i < chain.joints.size(); i++) {
                glm::quat& q = pose->joint[chain.joints[i]];
                q = glm::normalize(glm::slerp(before[i], q, total));
            }
        }
        chain.solved = true;
        chain.iterations = solve.iterations;
        chain.position_error = solve.position_error;
        chain.rotation_error = solve.rotation_error;
        chain.limit_excess = solve.limit_excess;
    }

}

void RigPoseApplySys::tick(double h) {

    OPTICK_EVENT();

    auto* bot = reg.ctx().find<HumanBot>();
    if(!bot || !bot->built) return;
    if(!bot->kinematic) {
        static bool warned = false;
        if(!warned) dy::log(dy::error) << "RigPoseApply: the HumanBot is not kinematic\n";
        warned = true;
        return;
    }
    auto& bodies = physicsWorld(reg).bodies();
    SegmentPoses poses;
    for(auto [e, rig, pose] : reg.view<Rig, RigPose>().each()) {
        rig.model->forward(pose, poses);
        for(size_t i = 0; i < bot->bodies.size(); i++) {
            JPH::RVec3 p(toJolt(poses.position[i]));
            JPH::Quat q = toJolt(glm::normalize(poses.rotation[i]));
            if(pose.teleport || h <= 0.) bodies.SetPositionAndRotation(bot->bodies[i], p, q, JPH::EActivation::Activate);
            else bodies.MoveKinematic(bot->bodies[i], p, q, (float) h);
        }
        pose.teleport = false;
    }

}

void dy::registerIK() {
    registerSystem("Fabrik", systemInfo<FabrikSys>(SystemScope::World));
    registerSystem("RigPoseApply", systemInfo<RigPoseApplySys>(SystemScope::World));
}
