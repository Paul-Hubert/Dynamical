#include "rig_pose.h"

#include <algorithm>
#include <cmath>

#include "humanbot.h"

using namespace dy;

SwingTwist dy::swingTwist(glm::quat q) {
    if(q.w < 0.f) q = -q;
    SwingTwist st;
    float length = std::sqrt(q.w * q.w + q.x * q.x);
    glm::quat twist = length > 1e-6f ? glm::quat(q.w / length, q.x / length, 0.f, 0.f) : glm::quat(1.f, 0.f, 0.f, 0.f);
    st.twist = 2.f * std::atan2(twist.x, twist.w);
    st.swing = q * glm::inverse(twist);
    if(st.swing.w < 0.f) st.swing = -st.swing;
    st.swing.x = 0.f;
    st.swing = glm::normalize(st.swing);
    return st;
}

glm::quat dy::fromSwingTwist(const SwingTwist& st) {
    return glm::normalize(st.swing * glm::angleAxis(st.twist, glm::vec3(1.f, 0.f, 0.f)));
}

RigModel::RigModel(std::shared_ptr<const BodyDef> def) : body(std::move(def)) {

    segments = (int) body->segments.size();
    joints = (int) body->joints.size();
    joint_of_segment.assign(segments, -1);
    frame1.resize(joints);
    frame2.resize(joints);
    rest_frame.resize(joints);
    anchor_parent.resize(joints);
    anchor_child.resize(joints);
    limits.resize(joints);

    for(int j = 0; j < joints; j++) {
        const JointDef& joint = body->joints[j];
        const SegmentDef& parent = body->segments[joint.parent];
        const SegmentDef& child = body->segments[joint.child];
        joint_of_segment[joint.child] = j;

        // As the ragdoll builder: X twist, Y plane, Z normal
        glm::vec3 x = glm::normalize(joint.twist_axis), y = glm::normalize(joint.plane_axis - x * glm::dot(joint.plane_axis, x));
        glm::quat constraint = glm::normalize(glm::quat_cast(glm::mat3(x, y, glm::cross(x, y))));
        rest_frame[j] = constraint;
        frame1[j] = glm::normalize(glm::inverse(parent.rest_rotation) * constraint);
        frame2[j] = glm::normalize(glm::inverse(child.rest_rotation) * constraint);
        anchor_parent[j] = glm::inverse(parent.rest_rotation) * (joint.anchor - parent.rest_position);
        anchor_child[j] = glm::inverse(child.rest_rotation) * (joint.anchor - child.rest_position);
        limits[j] = JointLimit { joint.twist_min, joint.twist_max, joint.swing_y, joint.swing_z };
    }
    body_limits = limits;

    // Segments are parents first in the body file: order the joints by their child
    order.resize(joints);
    for(int j = 0; j < joints; j++) order[j] = j;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return body->joints[a].child < body->joints[b].child; });

    mass.resize(segments);
    float total = 0.f;
    for(int i = 0; i < segments; i++) total += body->segments[i].percentage;
    for(int i = 0; i < segments; i++) mass[i] = body->segments[i].percentage / total;

}

int RigModel::jointIndex(const std::string& name) const {
    for(int j = 0; j < joints; j++) {
        if(body->joints[j].name == name) return j;
    }
    return -1;
}

RigPose RigModel::restPose() const {
    RigPose pose;
    pose.root_position = body->segments[body->pelvis].rest_position;
    pose.root_rotation = body->segments[body->pelvis].rest_rotation;
    pose.joint.assign(joints, glm::quat(1.f, 0.f, 0.f, 0.f));
    return pose;
}

RigPose RigModel::spawnPose() const {
    RigPose pose;
    pose.root_position = body->segments[body->pelvis].spawn_position;
    pose.root_rotation = body->segments[body->pelvis].spawn_rotation;
    pose.joint.resize(joints);
    for(int j = 0; j < joints; j++) {
        const JointDef& joint = body->joints[j];
        pose.joint[j] = jointRotation(j, body->segments[joint.parent].spawn_rotation, body->segments[joint.child].spawn_rotation);
    }
    return pose;
}

void RigModel::forward(const RigPose& pose, SegmentPoses& out) const {
    out.position.resize(segments);
    out.rotation.resize(segments);
    out.position[body->pelvis] = pose.root_position;
    out.rotation[body->pelvis] = pose.root_rotation;
    for(int j : order) {
        const JointDef& joint = body->joints[j];
        glm::quat rotation = childRotation(j, out.rotation[joint.parent], pose.joint[j]);
        out.rotation[joint.child] = rotation;
        out.position[joint.child] = out.position[joint.parent] + out.rotation[joint.parent] * anchor_parent[j] - rotation * anchor_child[j];
    }
}

glm::quat RigModel::clampTo(const JointLimit& limit, glm::quat q, float* excess) {

    q = glm::normalize(q);
    SwingTwist st = swingTwist(q);
    st.twist = std::clamp(st.twist, limit.twist_min, limit.twist_max);

    float a = std::sin(0.5f * limit.swing_y), b = std::sin(0.5f * limit.swing_z);
    float y = st.swing.y, z = st.swing.z;
    if(a < 1e-5f) y = 0.f;
    if(b < 1e-5f) z = 0.f;
    float u = a < 1e-5f ? 0.f : y / a, v = b < 1e-5f ? 0.f : z / b;
    float value = std::sqrt(u * u + v * v);
    if(value > 1.f) {
        y /= value;
        z /= value;
    }
    st.swing = glm::quat(std::sqrt(std::max(0.f, 1.f - y * y - z * z)), 0.f, y, z);

    glm::quat result = fromSwingTwist(st);
    if(excess) {
        float d = std::min(1.f, std::abs(glm::dot(q, result)));
        *excess = 2.f * std::acos(d);
        if(*excess < 2e-4f) *excess = 0.f;      // acos near 1: float noise
    }
    return result;

}

float RigModel::excess(const JointLimit& limit, glm::quat q) {
    float e = 0.f;
    clampTo(limit, q, &e);
    return e;
}

void RigModel::enlarge(int j, float twist_low_deg, float twist_high_deg, float swing_y_deg, float swing_z_deg) {
    const float rad = 3.14159265358979f / 180.f, most = 179.f * rad;
    limits[j].twist_min = std::max(limits[j].twist_min - twist_low_deg * rad, -most);
    limits[j].twist_max = std::min(limits[j].twist_max + twist_high_deg * rad, most);
    limits[j].swing_y = std::min(limits[j].swing_y + swing_y_deg * rad, most);
    limits[j].swing_z = std::min(limits[j].swing_z + swing_z_deg * rad, most);
}

glm::vec3 RigModel::centerOfMass(const SegmentPoses& poses) const {
    glm::vec3 sum(0.f);
    for(int i = 0; i < segments; i++) {
        sum += mass[i] * (poses.position[i] + poses.rotation[i] * body->segments[i].center_of_mass);
    }
    return sum;
}

entt::entity dy::ensureRig(entt::registry& reg) {
    auto* bot = reg.ctx().find<HumanBot>();
    if(!bot || !bot->body) return entt::null;
    auto view = reg.view<HumanBotSpawn>();
    if(view.begin() == view.end()) return entt::null;
    entt::entity e = *view.begin();
    if(!reg.all_of<Rig>(e)) {
        auto model = std::make_shared<RigModel>(bot->body);
        reg.emplace<Rig>(e, Rig { model });
        reg.emplace_or_replace<RigPose>(e, model->spawnPose());
    }
    return e;
}
