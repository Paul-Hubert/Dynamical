#include "gait.h"

#include <algorithm>
#include <cmath>

#include "ground_field.h"
#include "humanbot.h"
#include "scene/component_registry.h"
#include "scene/system_registry.h"
#include "util/log.h"

using namespace dy;

namespace {

constexpr float pi = 3.14159265358979f;
constexpr float gravity = 9.81f;
const glm::vec3 up(0.f, 1.f, 0.f);

glm::quat yawQuat(float a) { return glm::angleAxis(a, glm::vec3(0.f, 1.f, 0.f)); }
glm::quat pitchQuat(float a) { return glm::angleAxis(a, glm::vec3(1.f, 0.f, 0.f)); }    // positive swings a hanging limb forward
glm::quat rollQuat(float a) { return glm::angleAxis(a, glm::vec3(0.f, 0.f, 1.f)); }     // positive raises the right side

glm::vec3 headingDir(float yaw) { return glm::vec3(-std::sin(yaw), 0.f, -std::cos(yaw)); }

float smooth(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float wrapAngle(float a) {
    while(a > pi) a -= 2.f * pi;
    while(a < -pi) a += 2.f * pi;
    return a;
}

float approach(float value, float target, float step) {
    return value + std::clamp(target - value, -step, step);
}

// Sole frame: x right, y the normal, -z toward the toes
glm::quat soleFrame(glm::vec3 toes, glm::vec3 normal) {
    toes = toes - normal * glm::dot(toes, normal);
    float l = glm::length(toes);
    toes = l > 1e-6f ? toes / l : glm::vec3(0.f, 0.f, -1.f);
    return glm::normalize(glm::quat_cast(glm::mat3(glm::cross(toes, normal), normal, -toes)));
}

glm::vec3 soleCorner(const FootGeometry& g, const Footprint& p, int i) {
    return p.centre + p.rotation * glm::vec3((i & 1) ? g.half_width : -g.half_width, 0.f, i < 2 ? g.half_length : -g.half_length);
}

glm::quat flatRotation(const FootGeometry& g, const Footprint& p) {
    return glm::normalize(p.rotation * glm::inverse(g.sole_rotation));
}

// Convex hull on x z (monotone chain), counter clockwise
std::vector<glm::vec2> convexHull(std::vector<glm::vec2> p) {
    if(p.size() < 3) return p;
    std::sort(p.begin(), p.end(), [](glm::vec2 a, glm::vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    auto cross = [](glm::vec2 o, glm::vec2 a, glm::vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    std::vector<glm::vec2> hull(2 * p.size());
    size_t k = 0;
    for(size_t i = 0; i < p.size(); i++) {
        while(k >= 2 && cross(hull[k - 2], hull[k - 1], p[i]) <= 0.f) k--;
        hull[k++] = p[i];
    }
    for(size_t i = p.size() - 1, t = k + 1; i > 0; i--) {
        while(k >= t && cross(hull[k - 2], hull[k - 1], p[i - 1]) <= 0.f) k--;
        hull[k++] = p[i - 1];
    }
    hull.resize(k - 1);
    return hull;
}

// Distance from p to a convex polygon, 0 inside
float outsideDistance(const std::vector<glm::vec2>& hull, glm::vec2 p) {
    if(hull.size() < 3) return 0.f;
    bool inside = true;
    float best = 1e9f;
    for(size_t i = 0; i < hull.size(); i++) {
        glm::vec2 a = hull[i], b = hull[(i + 1) % hull.size()];
        glm::vec2 ab = b - a, ap = p - a;
        if(ab.x * ap.y - ab.y * ap.x < 0.f) inside = false;
        float t = std::clamp(glm::dot(ap, ab) / std::max(glm::dot(ab, ab), 1e-12f), 0.f, 1.f);
        best = std::min(best, glm::length(p - (a + ab * t)));
    }
    return inside ? 0.f : best;
}

struct GaitRig {
    entt::entity entity = entt::null;
    Rig* rig = nullptr;
    RigPose* pose = nullptr;
    GaitCommand* command = nullptr;
    GaitParams* params = nullptr;
    GaitState* state = nullptr;
};

GaitRig gaitRig(entt::registry& reg) {
    GaitRig g;
    g.entity = ensureRig(reg);
    if(g.entity == entt::null) return g;
    g.rig = &reg.get<Rig>(g.entity);
    g.pose = &reg.get<RigPose>(g.entity);
    g.command = &reg.get_or_emplace<GaitCommand>(g.entity);
    g.params = &reg.get_or_emplace<GaitParams>(g.entity);
    g.state = &reg.get_or_emplace<GaitState>(g.entity);
    return g;
}

// Where the knee of a foot at this rotation points: between the rest pose's forward and the toes, both carried by the
// foot. With the leg's plane through the foot's long axis the ankle only flexes and rolls: rolling over a sole edge
// (the sole toes out) and standing across a slope ask no yaw of it; the hip turns the leg instead
// In a turn the foot stays where it was planted while the pelvis turns: the knee points between the two, so the
// hip and the ankle share the yaw
glm::vec3 kneePole(const FootGeometry& g, glm::quat rotation, const GaitParams& params, glm::vec3 forward) {
    glm::vec3 foot = glm::normalize(rotation * glm::mix(g.pole, g.sole_rotation * glm::vec3(0.f, 0.f, -1.f), params.knee_out));
    return glm::normalize(glm::mix(forward, foot, params.knee_follow));
}

// The foot segment on its print at the sole pitch f.pitch
void placeStance(FootState& f) {
    footPose(f.geometry, f.current, f.pitch, f.pitch >= 0.f ? +1 : -1, f.position, f.rotation);
    f.contact = f.pitch > 1e-4f ? FootContact::Heel : (f.pitch < -1e-4f ? FootContact::Toe : FootContact::Flat);
}

// The sole pitch the stride asks for at share u of the stance: heel rocker, flat, toe rocker
void stancePitch(const GaitParams& params, const GaitState& state, FootState& f, float u) {
    if(u < params.flat_start) {
        f.pitch = f.strike * (1.f - smooth(u / params.flat_start));
    } else if(u < params.heel_off) {
        f.pitch = 0.f;
    } else {
        if(!f.toe_latched) {
            f.toe = params.toe_angle * state.amplitude;
            f.toe_latched = true;
        }
        f.pitch = -f.toe * smooth((u - params.heel_off) / (1.f - params.heel_off));
    }
}

// The swing foot's pose at share sw of its swing, without the lift
void swingPose(const GaitParams& params, const FootState& f, float sw, glm::vec3& position, glm::quat& rotation) {
    glm::vec3 to_position;
    glm::quat to_rotation;
    footPose(f.geometry, f.next, f.strike, f.strike >= 0.f ? +1 : -1, to_position, to_rotation);
    // The foot leaves fast enough to stay under the hip that walks away from it
    float e = smooth(sw);
    position = glm::mix(f.from_position, to_position, e);
    // The toes come up early, as the ankle does after pushing off: a foot hanging toes down would need more room
    rotation = glm::normalize(glm::slerp(f.from_rotation, to_rotation, smooth(sw / params.swing_level)));
}

}

glm::vec3 FootGeometry::corner(int i, glm::vec3 position, glm::quat rotation) const {
    glm::vec3 local((i & 1) ? half_width : -half_width, 0.f, i < 2 ? half_length : -half_length);
    return position + rotation * (sole_centre + sole_rotation * local);
}

FootGeometry dy::footGeometry(const RigModel& model, bool right) {

    const BodyDef& def = *model.body;
    FootGeometry g;
    g.segment = right ? def.right_foot : def.left_foot;
    g.side = right ? 1.f : -1.f;
    g.hip = model.jointIndex(right ? "RightUpLeg" : "LeftUpLeg");
    g.knee = model.jointIndex(right ? "RightLeg" : "LeftLeg");
    g.ankle = model.jointIndex(right ? "RightFoot" : "LeftFoot");
    if(g.hip < 0 || g.knee < 0 || g.ankle < 0) throw AssetError("gait: the body has no UpLeg, Leg and Foot joints");

    const SegmentDef& seg = def.segments[g.segment];
    const ShapeDef* box = nullptr;
    for(const ShapeDef& s : seg.shapes) {
        if(s.type == ShapeDef::Type::Box) { box = &s; break; }
    }
    if(!box) throw AssetError("gait: the foot " + seg.name + " has no box");

    const glm::vec3 forward(0.f, 0.f, -1.f);
    glm::quat world = seg.rest_rotation * box->rotation;
    glm::vec3 axes[3] = { world * glm::vec3(1.f, 0.f, 0.f), world * glm::vec3(0.f, 1.f, 0.f), world * glm::vec3(0.f, 0.f, 1.f) };
    float half[3] = { box->half_extents.x, box->half_extents.y, box->half_extents.z };
    int t = 0;
    for(int i = 1; i < 3; i++) if(std::abs(axes[i].y) > std::abs(axes[t].y)) t = i;
    int a = (t + 1) % 3, b = (t + 2) % 3;
    int l = half[a] >= half[b] ? a : b, w = l == a ? b : a;
    glm::vec3 normal = axes[t] * (axes[t].y >= 0.f ? 1.f : -1.f);
    glm::vec3 toes = axes[l] * (glm::dot(axes[l], forward) >= 0.f ? 1.f : -1.f);
    glm::quat sole = soleFrame(toes, normal);
    glm::vec3 centre = seg.rest_position + seg.rest_rotation * box->center - normal * half[t];
    g.sole_rotation = glm::normalize(glm::inverse(seg.rest_rotation) * sole);
    g.sole_centre = glm::inverse(seg.rest_rotation) * (centre - seg.rest_position);
    g.half_width = half[w];
    g.half_length = half[l];
    g.toe_out = std::atan2(-toes.x, -toes.z);
    g.pole = glm::inverse(seg.rest_rotation) * forward;
    return g;

}

HandGeometry dy::handGeometry(const RigModel& model, bool right) {

    const BodyDef& def = *model.body;
    HandGeometry g;
    g.shoulder = model.jointIndex(right ? "RightArm" : "LeftArm");
    g.elbow = model.jointIndex(right ? "RightForeArm" : "LeftForeArm");
    g.wrist = model.jointIndex(right ? "RightHand" : "LeftHand");
    if(g.shoulder < 0 || g.elbow < 0 || g.wrist < 0) throw AssetError("gait: the body has no Arm, ForeArm and Hand joints");

    FootGeometry& palm = g.palm;
    palm.segment = def.joints[g.wrist].child;
    palm.side = right ? 1.f : -1.f;
    palm.hip = g.shoulder;
    palm.knee = g.elbow;
    palm.ankle = g.wrist;
    const SegmentDef& seg = def.segments[palm.segment];
    const ShapeDef* box = nullptr;
    for(const ShapeDef& s : seg.shapes) {
        if(s.type == ShapeDef::Type::Box) { box = &s; break; }
    }
    if(!box) throw AssetError("gait: the hand " + seg.name + " has no box");

    glm::quat world = seg.rest_rotation * box->rotation;
    glm::vec3 axes[3] = { world * glm::vec3(1.f, 0.f, 0.f), world * glm::vec3(0.f, 1.f, 0.f), world * glm::vec3(0.f, 0.f, 1.f) };
    float half[3] = { box->half_extents.x, box->half_extents.y, box->half_extents.z };
    // The palm is one of the box's two widest faces, the fingers run along its longest side, away from the wrist
    int t = 0, l = 0;
    for(int i = 1; i < 3; i++) {
        if(half[i] < half[t]) t = i;
        if(half[i] > half[l]) l = i;
    }
    if(l == t) l = (t + 1) % 3;
    int w = 3 - t - l;
    const glm::vec3 shoulder = def.joints[g.shoulder].anchor, elbow = def.joints[g.elbow].anchor, wrist = def.joints[g.wrist].anchor;
    const glm::vec3 centre = seg.rest_position + seg.rest_rotation * box->center;
    glm::vec3 fingers = axes[l] * (glm::dot(axes[l], centre - wrist) >= 0.f ? 1.f : -1.f);
    // At the rest pose the arms hang, palms toward the thighs and a little backward
    glm::vec3 facing = axes[t] * (glm::dot(axes[t], glm::vec3(-palm.side, 0.f, 0.5f)) >= 0.f ? 1.f : -1.f);
    glm::quat frame = soleFrame(fingers, -facing);
    palm.sole_rotation = glm::normalize(glm::inverse(seg.rest_rotation) * frame);
    palm.sole_centre = glm::inverse(seg.rest_rotation) * (centre + facing * half[t] - seg.rest_position);
    palm.half_width = half[w];
    palm.half_length = half[l];

    g.reach = glm::length(elbow - shoulder) + glm::length(wrist - elbow);
    // The elbow flexes about its twist axis, to the side most of its range is on: it points the other way
    const JointLimit& limit = model.body_limits[g.elbow];
    glm::vec3 hinge = model.rest_frame[g.elbow] * glm::vec3(1.f, 0.f, 0.f);
    glm::vec3 flex = glm::cross(hinge, wrist - elbow) * (limit.twist_max >= -limit.twist_min ? 1.f : -1.f);
    g.pole_rest = glm::length(flex) > 1e-6f ? -glm::normalize(flex) : glm::vec3(palm.side, 0.f, 0.f);
    return g;

}

Footprint dy::fitFootprint(const GroundField& ground, const FootGeometry& foot, glm::vec2 centre, float yaw, float from_y, float max_tilt) {

    Footprint p;
    const glm::vec3 toes = headingDir(yaw);
    float y = ground.height(centre.x, centre.y, from_y, from_y - 1.5f);
    p.centre = glm::vec3(centre.x, y, centre.y);
    p.rotation = soleFrame(toes, up);

    glm::vec3 g[9];
    auto sample = [&](glm::vec3 at) {
        auto hit = ground.below(glm::vec3(at.x, from_y, at.z), 64.f);
        return hit ? hit->point : glm::vec3(at.x, p.centre.y, at.z);
    };
    glm::vec3 normal = up;
    for(int pass = 0; pass < 2; pass++) {
        for(int i = 0; i < 4; i++) g[i] = sample(soleCorner(foot, p, i));
        normal = glm::cross(g[3] - g[0], g[2] - g[1]);
        float l = glm::length(normal);
        normal = l > 1e-9f ? normal / l : up;
        if(normal.y < 0.f) normal = -normal;
        if(normal.y < std::cos(max_tilt)) {
            // A wall: the sole keeps the steepest tilt it is allowed, facing the same way
            glm::vec3 flat(normal.x, 0.f, normal.z);
            float l = glm::length(flat);
            normal = l > 1e-6f ? flat / l * std::sin(max_tilt) + up * std::cos(max_tilt) : up;
        }
        glm::vec3 mean = 0.25f * (g[0] + g[1] + g[2] + g[3]);
        p.centre.y = mean.y - (normal.x * (centre.x - mean.x) + normal.z * (centre.y - mean.z)) / normal.y;
        p.rotation = soleFrame(toes, normal);
    }

    // On ground that is not a plane the sole rests on its highest points
    for(int i = 0; i < 4; i++) g[i] = sample(soleCorner(foot, p, i));
    g[4] = sample(p.centre);
    g[5] = sample(0.5f * (soleCorner(foot, p, 0) + soleCorner(foot, p, 1)));
    g[6] = sample(0.5f * (soleCorner(foot, p, 2) + soleCorner(foot, p, 3)));
    g[7] = sample(0.5f * (soleCorner(foot, p, 0) + soleCorner(foot, p, 2)));
    g[8] = sample(0.5f * (soleCorner(foot, p, 1) + soleCorner(foot, p, 3)));
    float highest = -1e9f, lowest = 1e9f;
    for(int i = 0; i < 9; i++) {
        float d = glm::dot(normal, g[i] - p.centre);
        highest = std::max(highest, d);
        lowest = std::min(lowest, d);
    }
    p.centre += normal * highest;
    p.gap = highest - lowest;
    return p;

}

void dy::footPose(const FootGeometry& foot, const Footprint& print, float pitch, int pivot, glm::vec3& position, glm::quat& rotation) {
    rotation = flatRotation(foot, print);
    position = print.centre - rotation * foot.sole_centre;
    if(pitch == 0.f || pivot == 0) return;
    glm::vec3 axis = print.rotation * glm::vec3(1.f, 0.f, 0.f);
    glm::vec3 edge = print.centre + print.rotation * glm::vec3(0.f, 0.f, pivot > 0 ? foot.half_length : -foot.half_length);
    glm::quat turn = glm::angleAxis(pitch, axis);
    position = edge + turn * (position - edge);
    rotation = glm::normalize(turn * rotation);
}

// GaitBase

void GaitBaseSys::tick(double h) {

    OPTICK_EVENT();

    GaitRig g = gaitRig(reg);
    if(g.entity == entt::null) return;
    const RigModel& model = *g.rig->model;
    const BodyDef& def = *model.body;
    const GaitParams& params = *g.params;
    GaitState& state = *g.state;
    RigPose& pose = *g.pose;
    GroundField& ground = groundField(reg);
    const float dt = (float) h;

    if(!state.started) {
        for(int i = 0; i < 2; i++) state.foot[i].geometry = footGeometry(model, i == 1);
        const FootGeometry& left = state.foot[0].geometry;
        const SegmentDef& foot = def.segments[left.segment];
        float sole_y = (foot.rest_position + foot.rest_rotation * left.sole_centre).y;
        glm::vec3 hip = def.joints[left.hip].anchor, knee = def.joints[left.knee].anchor, ankle = def.joints[left.ankle].anchor;
        state.leg_length = hip.y - sole_y;
        state.reach = params.leg_extension * (glm::length(knee - hip) + glm::length(ankle - knee));
        // The hip is no nearer to the ankle than the knee folds
        {
            const float thigh = glm::length(knee - hip), shank = glm::length(ankle - knee);
            const float flexion = std::max(model.limits[left.knee].twist_max, 0.3f);
            state.fold = std::sqrt(thigh * thigh + shank * shank + 2.f * thigh * shank * std::cos(flexion));
        }
        state.pivot = 0.5f * (model.anchor_parent[state.foot[0].geometry.hip] + model.anchor_parent[state.foot[1].geometry.hip]);
        state.heading = g.command->heading;
        state.speed = 0.f;
        auto hit = ground.below(glm::vec3(g.command->start.x, 4096.f, g.command->start.y), 8192.f);
        state.centre = glm::vec3(g.command->start.x, hit ? hit->point.y : 0.f, g.command->start.y);
        float share = std::clamp(params.stance_share - params.stance_share_rate * params.reference_speed, 0.55f, 0.72f);
        state.phase = 0.5f * (share - 0.5f);    // the middle of a double support
        state.started = true;
        pose.teleport = true;
    }

    // The command, at the rates the walker can follow
    // Slopes slow the walker: the one under its feet, and the one it is about to reach
    {
        float steepness = std::hypot(std::tan(state.slope_pitch), std::tan(state.slope_roll));
        if(state.started) {
            const glm::vec3 forward = state.direction();
            float here = ground.height(state.centre.x, state.centre.z, state.centre.y + 2.f, state.centre.y);
            for(float d : { 0.5f * params.look_ahead, params.look_ahead }) {
                glm::vec3 at = state.centre + forward * d;
                float there = ground.height(at.x, at.z, state.centre.y + 2.f + 4.f * d, here);
                steepness = std::max(steepness, std::abs(there - here) / d);
            }
        }
        state.steepness = steepness;
        float wanted = std::max(g.command->speed, 0.f) * std::max(std::exp(-params.slope_slowdown * steepness), params.slope_speed_floor);
        // No step climbs or drops more than step_rise: on a wall the steps are short
        if(steepness > 1e-3f && params.step_rise > 0.f) wanted = std::min(wanted, std::max(2.f * params.step_rise / (params.max_stride_time * steepness), 1.2f * params.min_speed));
        state.wanted_speed = g.command->speed > params.min_speed ? wanted : 0.f;
        state.speed = approach(state.speed, state.wanted_speed, (state.wanted_speed < state.speed ? params.braking : params.acceleration) * dt);
    }
    state.heading = wrapAngle(state.heading + std::clamp(wrapAngle(g.command->heading - state.heading), -params.turn_rate * dt, params.turn_rate * dt));
    state.moving = g.command->speed > params.min_speed || state.speed > params.min_speed;

    // Gait laws
    const float leg = state.leg_length;
    auto law = [&](float v) { return leg * params.step_factor * std::pow(v / std::sqrt(gravity * leg), params.step_exponent); };
    const float v = std::max(state.speed, params.min_speed);
    state.stride_time = std::min(2.f * law(v) / v, params.max_stride_time);
    state.step_length = state.speed * state.stride_time * 0.5f;
    state.stance_share = std::clamp(params.stance_share + params.stance_share_rate * (state.speed - params.reference_speed), 0.55f, 0.72f);
    state.amplitude = std::clamp(state.step_length / law(params.reference_speed), 0.f, 1.25f);
    const auto* balance = reg.try_get<BalanceState>(g.entity);
    float com_height = balance && balance->valid ? balance->com.y - balance->ground : 1.08f * leg;
    state.omega = std::sqrt(gravity / std::max(com_height, 0.3f));
    const float step_time = 0.5f * state.stride_time;
    state.capture_offset = state.step_length / (std::exp(state.omega * step_time) - 1.f);

    // The clock stops only when both feet are down side by side: a walker that stops takes the step that brings
    // its feet together
    bool both_down = true;
    if(state.feet_ready) {
        float apart = glm::dot(state.foot[0].current.centre - state.foot[1].current.centre, state.direction());
        both_down = state.foot[0].stance && state.foot[1].stance && std::abs(apart) < 0.06f;
    }
    if(state.moving || !both_down) {
        state.phase += dt / state.stride_time;
        state.phase -= std::floor(state.phase);
    }
    state.time += h;

    const glm::vec3 dir = state.direction(), right = state.right();
    state.centre += dir * state.speed * dt;
    state.centre.y = ground.height(state.centre.x, state.centre.z, state.centre.y + 2.f, state.centre.y);

    // The slope under the walker, from its prints
    glm::vec3 normal = up;
    if(state.feet_ready) {
        normal = glm::normalize(state.foot[0].current.rotation * up + state.foot[1].current.rotation * up);
    } else if(auto hit = ground.below(state.centre + up * 2.f, 8.f)) {
        normal = hit->normal;
    }
    const float blend = dt / (0.3f + dt);
    state.slope_pitch += blend * (std::atan2(-glm::dot(normal, dir), normal.y) - state.slope_pitch);
    state.slope_roll += blend * (std::atan2(-glm::dot(normal, right), normal.y) - state.slope_roll);
    float lean = params.lean * std::min(state.speed / params.reference_speed, 1.5f) + params.lean_slope_gain * state.slope_pitch;
    state.lean += blend * (lean - state.lean);

    // Scrambling: the steeper the ground, the lower the trunk
    {
        const float along = std::tan(state.slope_pitch), across = std::tan(state.slope_roll);
        const float steep = std::hypot(along, across), slope = std::atan(steep);
        const float low = params.scramble ? smooth((slope - params.scramble_from) / std::max(params.scramble_full - params.scramble_from, 1e-3f)) : 0.f;
        state.low += blend * (low - state.low);
        if(!params.scramble && state.low < 1e-4f) {
            state.low = 0.f;
            state.tilt = glm::vec2(0.f);
            state.forward = 0.f;
        }
        state.uphill = steep > 1e-4f ? (dir * along + right * across) / steep : glm::vec3(0.f);
    }
    const bool lowered = state.low > 0.f;

    // Pelvis: level to gravity, turning and listing with the stride
    const float phi = 2.f * pi * state.phase;
    const float side = std::sin(2.f * pi * (state.phase - 0.5f * state.stance_share + 0.25f));     // +1 in the middle of the left stance
    const float amplitude = std::min(state.amplitude, 1.f);
    const float gait_yaw = -params.pelvis_yaw * amplitude * std::cos(phi);
    const float gait_roll = -params.pelvis_list * amplitude * side;
    const glm::quat gait = yawQuat(gait_yaw) * rollQuat(gait_roll);

    // The centre of mass sways toward the stance foot as an inverted pendulum between the two feet does
    float sway = 0.5f * params.step_width * (1.f - 1.f / std::cosh(0.5f * state.omega * step_time)) * std::min(state.speed / 0.4f, 1.f);
    state.sway = -sway * side;
    const float ankle_height = state.leg_length - state.reach / params.leg_extension;
    // Highest in mid stance, lowest in the middle of each double support
    const float bob = params.bob * amplitude * amplitude * 0.5f * (1.f + std::cos(4.f * pi * (state.phase - 0.5f * (state.stance_share - 0.5f))));
    // ... and lower as the walker scrambles
    const float drop = lowered ? params.scramble_crouch * state.low * state.reach : 0.f;
    state.hip_centre = state.centre + right * state.sway + up * (params.hip_height * state.reach + ankle_height - bob - drop);
    state.com_target = glm::vec2(state.centre.x + right.x * state.sway, state.centre.z + right.z * state.sway);
    if(lowered) {
        // The body moves up the slope, less so across the heading, where it would spread the legs
        const glm::vec3 moved = dir * glm::dot(state.uphill, dir) + right * (0.7f * glm::dot(state.uphill, right));
        state.com_target += glm::vec2(moved.x, moved.z) * (state.forward * state.low);
    }

    auto set = [&](RigPose& p, const char* name, glm::quat delta) {
        int j = model.jointIndex(name);
        if(j >= 0) p.joint[j] = model.clamp(j, model.fromRestDelta(j, delta));
    };
    // The pelvis and the trunk. The trunk leans, undoes the pelvis' list and turns the shoulders against it; the head
    // keeps a level gaze. Scrambling, they tilt toward the slope by `tilt` (radians along the heading and to the
    // right): the spine bends as far as it may and the pelvis tips for the rest
    auto trunk = [&](RigPose& p, glm::vec2 tilt) {
        glm::quat spine = yawQuat(-0.5f * gait_yaw * (1.f + params.counter_rotation)) * rollQuat(-0.5f * gait_roll) * pitchQuat(-0.5f * state.lean);
        glm::quat pelvis(1.f, 0.f, 0.f, 0.f);
        if(lowered) {
            const float angle = glm::length(tilt);
            const float bend = std::min(angle, 2.f * params.scramble_spine);
            const glm::vec2 toward = angle > 1e-6f ? tilt / angle : glm::vec2(1.f, 0.f);
            pelvis = glm::angleAxis(angle - bend, glm::vec3(-toward.x, 0.f, -toward.y));
            // The walk's lean gives way to the tilt
            const glm::vec2 both = glm::vec2((1.f - state.low) * state.lean, 0.f) + toward * bend;
            const float a = glm::length(both);
            const glm::quat lean = a > 1e-6f ? glm::angleAxis(0.5f * a, glm::vec3(-both.x, 0.f, -both.y) / a) : glm::quat(1.f, 0.f, 0.f, 0.f);
            spine = yawQuat(-0.5f * gait_yaw * (1.f + params.counter_rotation)) * rollQuat(-0.5f * gait_roll) * lean;
            p.root_rotation = glm::normalize(yawQuat(state.heading) * gait * pelvis * def.segments[def.pelvis].rest_rotation);
        } else {
            p.root_rotation = glm::normalize(yawQuat(state.heading) * gait * def.segments[def.pelvis].rest_rotation);
        }
        p.root_position = state.hip_centre - p.root_rotation * state.pivot;
        set(p, "Spine", spine);
        set(p, "Spine2", spine);
        set(p, "Head", glm::inverse(lowered ? gait * pelvis * spine * spine : gait * spine * spine));
    };

    if(lowered) {
        // The trunk tilts toward the slope until the shoulders are within an arm of the ground, the hips where
        // GaitBalance had them on the last tick
        glm::vec2 wanted(0.f);
        const int left = model.jointIndex("LeftArm"), right_arm = model.jointIndex("RightArm");
        if(glm::length(state.uphill) > 0.5f && left >= 0 && right_arm >= 0) {
            const glm::vec2 toward(glm::dot(state.uphill, dir), glm::dot(state.uphill, right));
            const glm::vec3 moved = balance && balance->valid ? balance->offset : glm::vec3(0.f);
            auto gap = [&](float angle) {
                RigPose p = pose;
                trunk(p, toward * angle);
                p.root_position += moved;
                SegmentPoses at;
                model.forward(p, at);
                glm::vec3 chest = 0.5f * (model.anchor(at, left) + model.anchor(at, right_arm));
                auto hit = ground.below(chest + up * 1.5f, 8.f);
                return hit ? (chest.y - hit->point.y) * hit->normal.y : 1e3f;
            };
            // No further than a little past lying along the slope
            const float slope = std::atan(std::hypot(std::tan(state.slope_pitch), std::tan(state.slope_roll)));
            const float most = std::clamp(0.5f * pi - slope + params.scramble_past, 0.f, params.scramble_lean);
            float lo = 0.f, hi = most;
            const float upright = gap(lo), tilted = gap(hi);
            if(upright > params.scramble_shoulder) {
                if(tilted < params.scramble_shoulder) {
                    for(int pass = 0; pass < 10; pass++) {
                        float mid = 0.5f * (lo + hi);
                        (gap(mid) > params.scramble_shoulder ? lo : hi) = mid;
                    }
                    hi = 0.5f * (lo + hi);
                }
                wanted = toward * (hi * state.low);
            }
            // Still too far with all that tilt: the body moves up the slope, and comes back when there is tilt to spare
            const float short_of = tilted - params.scramble_shoulder + 0.03f;
            state.forward = std::clamp(state.forward + (dt / 0.4f) * short_of / std::max(std::sin(slope), 0.3f), 0.f, params.scramble_forward);
            state.shoulder_gap = gap(glm::length(state.tilt));
        }
        state.tilt += blend * (wanted - state.tilt);
    }
    trunk(pose, state.tilt);

    // Arms against the legs. Under a tilted trunk they hang as gravity has them
    glm::quat hang(1.f, 0.f, 0.f, 0.f);
    if(lowered && glm::length(state.tilt) > 1e-6f) {
        hang = glm::angleAxis(-glm::length(state.tilt), glm::normalize(glm::vec3(-state.tilt.x, 0.f, -state.tilt.y)));
    }
    const float swing = params.arm_swing * amplitude * std::cos(phi);
    for(int i = 0; i < 2; i++) {
        float angle = params.arm_bias * amplitude + (i == 0 ? -swing : swing);
        float forward = amplitude > 1e-3f ? 0.5f + 0.5f * (i == 0 ? -1.f : 1.f) * std::cos(phi) : 0.f;
        set(pose, i == 0 ? "LeftArm" : "RightArm", lowered ? hang * pitchQuat(angle) : pitchQuat(angle));
        set(pose, i == 0 ? "LeftForeArm" : "RightForeArm", pitchQuat(params.elbow * amplitude + params.elbow_swing * amplitude * forward));
        set(pose, i == 0 ? "LeftHand" : "RightHand", glm::quat(1.f, 0.f, 0.f, 0.f));
    }

}

// GaitFootIK

void GaitFootIKSys::tick(double h) {

    OPTICK_EVENT();

    GaitRig g = gaitRig(reg);
    if(g.entity == entt::null || !g.state->started) return;
    const RigModel& model = *g.rig->model;
    const GaitParams& params = *g.params;
    GaitState& state = *g.state;
    GroundField& ground = groundField(reg);
    const float dt = (float) h;
    const glm::vec3 dir = state.direction(), right = state.right();

    if(!state.feet_ready) {
        for(int i = 0; i < 2; i++) {
            FootState& f = state.foot[i];
            const FootGeometry& geo = f.geometry;
            glm::vec3 at = state.centre + right * (geo.side * 0.5f * params.step_width);
            f.current = f.next = fitFootprint(ground, geo, glm::vec2(at.x, at.z), state.heading + geo.toe_out, state.centre.y + 1.5f, params.max_print_tilt);
            f.stance = true;
            f.toe_latched = false;
            f.strike = f.toe = 0.f;
            f.chain = reg.create();
            auto& chain = reg.emplace<IKChain>(f.chain);
            chain.rig = g.entity;
            chain.name = i == 0 ? "left leg" : "right leg";
            chain.joints = { geo.hip, geo.knee, geo.ankle };
            chain.pole_rest = glm::vec3(0.f, 0.f, -1.f);
            g.rig->model->limits[geo.ankle] = g.rig->model->body_limits[geo.ankle];
            g.rig->model->enlarge(geo.ankle, 0.f, 0.f, glm::degrees(params.ankle_roll_extra), glm::degrees(params.ankle_yaw_extra));
            f.target = reg.create();
            auto& target = reg.emplace<IKTarget>(f.target);
            target.chain = f.chain;
            target.owner = "GaitFootIK";
        }
        state.ground_version = ground.version;
        state.feet_ready = true;
    }

    // The ground moved under the walker (a tilted test plane): the prints go back onto it
    if(state.ground_version != ground.version) {
        for(FootState& f : state.foot) {
            glm::vec3 toes = f.current.rotation * glm::vec3(0.f, 0.f, -1.f);
            f.current = fitFootprint(ground, f.geometry, glm::vec2(f.current.centre.x, f.current.centre.z), std::atan2(-toes.x, -toes.z), f.current.centre.y + 4.f, params.max_print_tilt);
            f.planned = false;
        }
        state.ground_version = ground.version;
    }

    const auto* balance = reg.try_get<BalanceState>(g.entity);

    // How far past its dorsiflexion stop a foot's ankle would be, radians. The hips are where GaitBase put them, moved
    // as GaitBalance moved them on the last tick
    RigPose trial = *g.pose;
    if(balance && balance->valid) trial.root_position += balance->offset;
    // The hip joints where the hips will be if they need not crouch
    glm::vec3 hip_joint[2];
    {
        RigPose high = *g.pose;
        if(balance && balance->valid) high.root_position += glm::vec3(balance->offset.x, 0.f, balance->offset.z);
        SegmentPoses poses;
        model.forward(high, poses);
        for(int i = 0; i < 2; i++) hip_joint[i] = model.anchor(poses, state.foot[i].geometry.hip);
    }

    // How far outside its swing cone a foot's ankle would be (0 inside; 0.1 is a tenth past the limit)
    auto swingOver = [&](const FootState& f) {
        const FootGeometry& geo = f.geometry;
        IKGoal goal { f.position + f.rotation * model.anchor_child[geo.ankle], f.rotation, true, kneePole(geo, f.rotation, params, dir) };
        solveChain(model, trial, { geo.hip, geo.knee, geo.ankle }, glm::vec3(0.f, 0.f, -1.f), goal, false);
        const JointLimit& limit = model.limits[geo.ankle];
        glm::quat swing = swingTwist(trial.joint[geo.ankle]).swing;
        return std::hypot(swing.y / std::sin(0.5f * limit.swing_y), swing.z / std::sin(0.5f * limit.swing_z)) - 1.f;
    };

    // Positive: past the dorsiflexion stop (the toes should drop); negative: past the plantarflexion stop
    auto flexionOverAt = [&](RigPose& body, const FootGeometry& geo, glm::vec3 position, glm::quat rotation) {
        IKGoal goal { position + rotation * model.anchor_child[geo.ankle], rotation, true, kneePole(geo, rotation, params, dir) };
        solveChain(model, body, { geo.hip, geo.knee, geo.ankle }, glm::vec3(0.f, 0.f, -1.f), goal, false);
        const JointLimit& limit = model.limits[geo.ankle];
        float twist = swingTwist(body.joint[geo.ankle]).twist;
        if(twist < limit.twist_min + params.ankle_margin) return limit.twist_min + params.ankle_margin - twist;
        if(twist > limit.twist_max - params.ankle_margin) return limit.twist_max - params.ankle_margin - twist;
        return 0.f;
    };
    auto flexionOver = [&](const FootState& f) { return flexionOverAt(trial, f.geometry, f.position, f.rotation); };

    for(int i = 0; i < 2; i++) {
        FootState& f = state.foot[i];
        const FootGeometry& geo = f.geometry;
        const float phase = state.footPhase(i), share = state.stance_share;
        f.landed = false;

        if(f.stance && phase >= share) {
            // Toe-off: the swing starts from where the rocker left the foot
            placeStance(f);
            f.from_position = f.position;
            f.from_rotation = f.rotation;
            f.stance = false;
            f.planned = false;
            f.contact = FootContact::None;
        } else if(!f.stance && phase < share) {
            f.current = f.next;
            f.extra = 0.f;
            f.stance = true;
            f.toe_latched = false;
            f.landed = true;
        }

        if(f.stance) {
            if(!state.moving) {
                // Standing, or closing the last step: the soles settle flat
                float keep = std::exp(-dt / 0.2f);
                f.strike *= keep;
                f.toe *= keep;
            }
            stancePitch(params, state, f, phase / share);
            const float stride_pitch = f.pitch;
            placeStance(f);

            // The heel rises as soon as the leg would be too short for a flat foot: the hips keep their height and
            // the foot rolls onto its toe edge (earlier uphill and in long steps). Not while the heel rocker lasts
            f.reach_relief = 0.f;
            auto stretch = [&](float pitch) {
                glm::vec3 position;
                glm::quat rotation;
                footPose(geo, f.current, pitch, pitch >= 0.f ? +1 : -1, position, rotation);
                return glm::length(position + rotation * model.anchor_child[geo.ankle] - hip_joint[i]) - state.reach;
            };
            // Only for a foot behind its hip, more as it falls behind: ahead of the hip a heel's rise does not bring
            // the ankle nearer
            const float trailing = smooth(-glm::dot(f.position - hip_joint[i], dir) / 0.2f);
            if(f.pitch <= 0.f && trailing > 0.f && stretch(f.pitch) > 0.f) {
                float low = f.pitch, high = -params.max_heel_rise;
                if(stretch(high) < 0.f) {
                    for(int pass = 0; pass < 14; pass++) {
                        float mid = 0.5f * (low + high);
                        (stretch(mid) > 0.f ? low : high) = mid;
                    }
                } else {
                    // Out of reach whatever the rise: the nearest the ankle comes, the hips crouch for the rest
                    for(int pass = 0; pass < 24; pass++) {
                        float a = low + (high - low) / 3.f, b = high - (high - low) / 3.f;
                        (stretch(a) < stretch(b) ? high : low) = stretch(a) < stretch(b) ? b : a;
                    }
                    high = 0.5f * (low + high);
                }
                // ... but no further than the ankle's swing allows on a side slope: the hips crouch for the rest
                const float asked = f.pitch;
                high = glm::mix(asked, high, trailing);
                f.pitch = high;
                placeStance(f);
                if(swingOver(f) > 0.f) {
                    float keep = 0.f, drop = 1.f;       // shares of the rise
                    for(int pass = 0; pass < 8; pass++) {
                        float mid = 0.5f * (keep + drop);
                        f.pitch = glm::mix(asked, high, mid);
                        placeStance(f);
                        (swingOver(f) > 0.f ? drop : keep) = mid;
                    }
                    f.pitch = glm::mix(asked, high, keep);
                    placeStance(f);
                }
                f.reach_relief = asked - f.pitch;
            }

            // An ankle at its dorsiflexion stop lifts the heel too, as it does in people; at its plantarflexion
            // stop the heel comes back down
            f.relief = 0.f;
            for(int pass = 0; pass < 4; pass++) {
                float over = flexionOver(f);
                if(std::abs(over) < 1e-4f) break;
                f.pitch -= over;
                f.relief += over;
                placeStance(f);
            }

            // A heel raised more than the stride asks comes back down no faster than heel_drop_rate: as the knee
            // straightens the ankle leaves its stop quickly, and the heel would drop under the leg at once
            float extra = stride_pitch - f.pitch;
            if(extra < f.extra - params.heel_drop_rate * dt) {
                extra = f.extra - params.heel_drop_rate * dt;
                f.pitch = stride_pitch - extra;
                placeStance(f);
            }
            f.extra = std::max(extra, 0.f);
            // However the ankle asks, a heel never rises past its toes
            if(f.pitch < -(params.max_heel_rise + 0.7f)) {
                f.pitch = -(params.max_heel_rise + 0.7f);
                placeStance(f);
            }
        } else {
            const float sw = (phase - share) / (1.f - share);
            if(!f.planned || sw < params.replan_until) {
                // The heel lands capture_offset behind where the extrapolated centre of mass will be
                // ... on the path the walker will have followed by then, turning at its rate toward the commanded heading
                const float remaining = (1.f - phase) * state.stride_time;
                float ahead = balance && balance->valid ? balance->lead : state.speed / state.omega;
                ahead = std::clamp(ahead, 0.f, 1.5f * state.speed / state.omega + 0.05f);
                const float turn = std::clamp(wrapAngle(g.command->heading - state.heading), -params.turn_rate * remaining, params.turn_rate * remaining);
                const float heading = state.heading + turn;
                const glm::vec3 then = headingDir(heading), then_right(std::cos(heading), 0.f, -std::sin(heading));
                glm::vec3 centre_then = state.centre + headingDir(state.heading + 0.5f * turn) * (state.speed * remaining);
                glm::vec3 heel = centre_then + then * (ahead - state.capture_offset) + then_right * (geo.side * 0.5f * params.step_width);
                const float yaw = heading + geo.toe_out;
                glm::vec3 centre = heel + headingDir(yaw) * geo.half_length;
                const float from_y = std::max(f.current.centre.y, state.centre.y) + 2.f + params.max_print_rise;
                f.next = fitFootprint(ground, geo, glm::vec2(centre.x, centre.z), yaw, from_y, params.max_print_tilt);
                // A landing too far above or below the other foot comes back toward where this foot left, until a
                // leg can span it (the edge of a drop, the foot of a wall)
                const float other = state.foot[1 - i].current.centre.y;
                if(std::abs(f.next.centre.y - other) > params.max_print_rise) {
                    glm::vec2 far(centre.x, centre.z), near(f.current.centre.x, f.current.centre.z);
                    float keep = 0.f, drop = 1.f;
                    for(int pass = 0; pass < 8; pass++) {
                        float mid = 0.5f * (keep + drop);
                        Footprint trial = fitFootprint(ground, geo, glm::mix(near, far, mid), yaw, from_y, params.max_print_tilt);
                        (std::abs(trial.centre.y - other) > params.max_print_rise ? drop : keep) = mid;
                    }
                    f.next = fitFootprint(ground, geo, glm::mix(near, far, keep), yaw, from_y, params.max_print_tilt);
                }
                // Uphill the foot lands flatter
                f.strike = std::clamp(params.strike_angle * state.amplitude - std::max(state.slope_pitch, 0.f), 0.f, params.strike_angle * 1.25f);
                // ... and no flatter than its ankle will allow under the hips as they will be then: on a steep climb
                // it lands on its toe edge
                {
                    RigPose then_pose = trial;
                    glm::vec3 moved = centre_then - state.centre;
                    moved.y = ground.height(centre_then.x, centre_then.z, from_y, state.centre.y) - state.centre.y;
                    then_pose.root_position += moved;
                    for(int pass = 0; pass < 3; pass++) {
                        glm::vec3 position;
                        glm::quat rotation;
                        footPose(geo, f.next, f.strike, f.strike >= 0.f ? +1 : -1, position, rotation);
                        float over = flexionOverAt(then_pose, geo, position, rotation);
                        if(std::abs(over) < 1e-4f) break;
                        f.strike = std::max(f.strike - over, -params.max_heel_rise);
                    }
                }

                // Enough lift for the sole to clear the ground on its way
                f.lift = params.swing_lift * std::clamp(state.amplitude, 0.3f, 1.f);
                for(int k = 2; k <= 6; k++) {
                    const float at = k / 8.f;
                    glm::vec3 position;
                    glm::quat rotation;
                    swingPose(params, f, at, position, rotation);
                    float gap = 1e9f;
                    for(int c = 0; c < 4; c++) {
                        glm::vec3 corner = geo.corner(c, position, rotation);
                        if(auto hit = ground.below(glm::vec3(corner.x, corner.y + 2.f, corner.z), 8.f)) gap = std::min(gap, corner.y - hit->point.y);
                    }
                    if(gap < 1e8f) f.lift = std::max(f.lift, (params.swing_clearance - gap) / std::sin(pi * at));
                }
                f.planned = true;
            }
            swingPose(params, f, sw, f.position, f.rotation);
            f.position += up * (f.lift * std::sin(pi * sw));
            f.contact = FootContact::None;

            // The ankle's stop holds in the air too: the toes drop, and the foot rises by what that takes from its
            // clearance
            f.relief = 0.f;
            for(int pass = 0; pass < 3; pass++) {
                float over = flexionOver(f);
                if(std::abs(over) < 1e-4f) break;
                f.rotation = glm::normalize(glm::angleAxis(-over, f.rotation * (geo.sole_rotation * glm::vec3(1.f, 0.f, 0.f))) * f.rotation);
                f.relief += over;
            }
            // ... and its swing cone: in the air the foot takes the rotation its ankle allows, and turns to its
            // landing rotation as it comes down
            {
                IKGoal goal { f.position + f.rotation * model.anchor_child[geo.ankle], f.rotation, true, kneePole(geo, f.rotation, params, dir) };
                RigPose held = trial;
                IKSolve solve = solveChain(model, held, { geo.hip, geo.knee, geo.ankle }, glm::vec3(0.f, 0.f, -1.f), goal, true, false);
                if(solve.rotation_error > 1e-4f) {
                    SegmentPoses poses;
                    model.forward(held, poses);
                    f.rotation = glm::normalize(glm::slerp(poses.rotation[geo.segment], f.rotation, smooth((sw - 0.6f) / 0.35f)));
                    f.relief += solve.rotation_error;
                }
            }
            if(f.relief != 0.f) {
                float gap = 1e9f;
                for(int c = 0; c < 4; c++) {
                    glm::vec3 corner = geo.corner(c, f.position, f.rotation);
                    if(auto hit = ground.below(glm::vec3(corner.x, corner.y + 2.f, corner.z), 8.f)) gap = std::min(gap, corner.y - hit->point.y);
                }
                const float least = params.swing_clearance * std::sin(pi * sw);
                if(gap < least) f.position.y += least - gap;
            }
        }

        f.pole = kneePole(geo, f.rotation, params, dir);
        reg.get<IKChain>(f.chain).yield = params.yield_limits;
        auto& target = reg.get<IKTarget>(f.target);
        target.position = f.position + f.rotation * model.anchor_child[geo.ankle];
        target.rotation = f.rotation;
        target.pole = f.pole;
        target.enabled = true;
    }

}

// GaitHandIK

void GaitHandIKSys::tick(double h) {

    OPTICK_EVENT();

    GaitRig g = gaitRig(reg);
    if(g.entity == entt::null || !g.state->started) return;
    const RigModel& model = *g.rig->model;
    const GaitParams& params = *g.params;
    GaitState& state = *g.state;
    if(!params.scramble && !state.hands_ready) return;
    GroundField& ground = groundField(reg);
    const float dt = (float) h;
    const glm::vec3 dir = state.direction(), right = state.right();

    if(!state.hands_ready) {
        for(int i = 0; i < 2; i++) {
            HandState& hand = state.hand[i];
            hand.geometry = handGeometry(model, i == 1);
            const HandGeometry& geo = hand.geometry;
            hand.chain = reg.create();
            auto& chain = reg.emplace<IKChain>(hand.chain);
            chain.rig = g.entity;
            chain.name = i == 0 ? "left arm" : "right arm";
            chain.joints = { geo.shoulder, geo.elbow, geo.wrist };
            chain.pole_rest = geo.pole_rest;
            hand.target = reg.create();
            auto& target = reg.emplace<IKTarget>(hand.target);
            target.chain = hand.chain;
            target.owner = "GaitHandIK";
            target.enabled = false;
        }
        state.hands_ready = true;
    }

    // The trunk where GaitBase put it, the hips moved as GaitBalance moved them on the last tick, the arms swinging
    const auto* balance = reg.try_get<BalanceState>(g.entity);
    RigPose trial = *g.pose;
    if(balance && balance->valid) trial.root_position += balance->offset;
    SegmentPoses poses;
    model.forward(trial, poses);

    // Where the elbow of an arm points, its hand at this pose: around the line from the shoulder to the wrist, where the
    // arm is the least outside its limits, near where a bent elbow rests (out and back) and where it pointed (`last`)
    // Returns the angle outside the limits there, radians
    auto elbowPole = [&](const HandGeometry& geo, glm::vec3 shoulder, glm::vec3 position, glm::quat rotation, glm::vec3 last, glm::vec3& pole) {
        const glm::vec3 wrist = position + rotation * model.anchor_child[geo.wrist];
        glm::vec3 axis = wrist - shoulder;
        axis = glm::length(axis) > 1e-6f ? glm::normalize(axis) : -up;
        glm::vec3 relaxed = right * (0.8f * geo.palm.side) - 0.4f * dir - 0.2f * up;
        relaxed -= axis * glm::dot(relaxed, axis);
        if(glm::length(relaxed) < 1e-4f) relaxed = glm::cross(axis, dir);
        relaxed = glm::normalize(relaxed);
        const glm::vec3 turned = glm::cross(axis, relaxed);
        const std::vector<int> joints { geo.shoulder, geo.elbow, geo.wrist };
        float least = 1e9f, over = 0.f;
        for(int k = 0; k < 12; k++) {
            const float angle = 2.f * pi * k / 12.f;
            glm::vec3 candidate = relaxed * std::cos(angle) + turned * std::sin(angle);
            RigPose free = trial;
            solveChain(model, free, joints, geo.pole_rest, IKGoal { wrist, rotation, true, candidate }, false, false);
            float excess = 0.f;
            for(int j : joints) excess = std::max(excess, RigModel::excess(model.limits[j], free.joint[j]));
            float cost = excess + 0.1f * (1.f - glm::dot(candidate, relaxed));
            if(glm::length(last) > 0.5f) cost += 0.2f * (1.f - glm::dot(candidate, last));
            if(cost < least) {
                least = cost;
                over = excess;
                pole = candidate;
            }
        }
        return over;
    };

    // A hold for a hand: where its palm lies flat on the ground within the arm's reach, the furthest ahead of the
    // shoulder (the walker comes to it, then leaves it behind), or the nearest to a relaxed arm when it stands
    // `over`: the angle the arm is outside its limits there
    float hold_over = 0.f;
    auto findHold = [&](const HandGeometry& geo, glm::vec3 shoulder, float least_ahead, Footprint& out) {
        const float reach = params.hand_plant_reach * geo.reach;
        const float palm = glm::length(geo.palm.sole_centre - model.anchor_child[geo.wrist]);
        const float from_y = shoulder.y + 0.6f;
        // Fingers ahead, and up the slope as the trunk comes down
        const glm::vec3 fingers = dir + state.uphill * (0.8f * state.low);
        const float yaw = std::atan2(-fingers.x, -fingers.z);
        bool found = false;
        float best = -1e9f;
        glm::vec2 where(0.f);
        for(float a : { 0.54f, 0.42f, 0.3f, 0.18f, 0.06f, -0.06f }) {
            if(a < least_ahead) continue;
            for(float b : { 0.06f, 0.18f, 0.3f }) {
                const float score = (state.moving ? a : -std::abs(a - 0.18f)) - 0.3f * b;
                if(score <= best) continue;
                glm::vec3 at = shoulder + dir * a + right * (geo.palm.side * b);
                auto hit = ground.below(glm::vec3(at.x, from_y, at.z), 3.f);
                if(!hit || glm::length(hit->point - shoulder) > reach + palm + 0.05f) continue;
                Footprint print = fitFootprint(ground, geo.palm, glm::vec2(at.x, at.z), yaw, from_y, params.hand_max_tilt);
                glm::vec3 position;
                glm::quat rotation;
                footPose(geo.palm, print, 0.f, 0, position, rotation);
                const float d = glm::length(position + rotation * model.anchor_child[geo.wrist] - shoulder);
                if(d > reach || d < 0.3f * geo.reach) continue;
                // The palm faces the ground from the shoulder's side of it
                if(glm::dot(shoulder - print.centre, print.rotation * up) < 0.08f) continue;
                best = score;
                out = print;
                where = glm::vec2(at.x, at.z);
                found = true;
            }
        }
        if(!found) return false;
        // The fingers turn, inward mostly, to where the arm is the least outside its limits
        glm::vec3 position, pole;
        glm::quat rotation;
        footPose(geo.palm, out, 0.f, 0, position, rotation);
        float least = elbowPole(geo, shoulder, position, rotation, glm::vec3(0.f), pole);
        for(float turn : { 0.5f, 1.f, 1.5f, -0.5f }) {
            if(least < 0.02f) break;
            Footprint print = fitFootprint(ground, geo.palm, where, yaw + geo.palm.side * turn, from_y, params.hand_max_tilt);
            footPose(geo.palm, print, 0.f, 0, position, rotation);
            const float d = glm::length(position + rotation * model.anchor_child[geo.wrist] - shoulder);
            if(d > reach || d < 0.3f * geo.reach) continue;
            float over = elbowPole(geo, shoulder, position, rotation, glm::vec3(0.f), pole);
            if(over < least - 0.02f) {
                least = over;
                out = print;
            }
        }
        hold_over = least;
        return true;
    };

    for(int i = 0; i < 2; i++) {
        HandState& hand = state.hand[i];
        const HandGeometry& geo = hand.geometry;
        const HandState& other = state.hand[1 - i];
        const glm::vec3 shoulder = model.anchor(poses, geo.shoulder);
        const glm::vec3 free_position = poses.position[geo.palm.segment];
        const glm::quat free_rotation = poses.rotation[geo.palm.segment];
        hand.planted_now = false;
        hand.wait = std::max(hand.wait - dt, 0.f);

        auto leave = [&](const Footprint& print) {
            if(hand.contact == HandContact::Free) {
                hand.from_position = free_position;
                hand.from_rotation = free_rotation;
            } else {
                hand.from_position = hand.position;
                hand.from_rotation = hand.rotation;
            }
            hand.next = print;
            hand.t = 0.f;
            hand.contact = HandContact::Reaching;
        };

        Footprint print;
        if(hand.contact == HandContact::Free || hand.contact == HandContact::Releasing) {
            if(params.scramble && hand.wait <= 0.f && findHold(geo, shoulder, -1.f, print)) leave(print);
        } else if(hand.contact == HandContact::Planted) {
            glm::vec3 position;
            glm::quat rotation;
            footPose(geo.palm, hand.current, 0.f, 0, position, rotation);
            const glm::vec3 wrist = position + rotation * model.anchor_child[geo.wrist];
            // Out of reach, the trunk's lean counted (GaitBalance, on the last tick)
            const bool out = !params.scramble || hand.deficit > 5e-3f || glm::length(wrist - shoulder) > geo.reach + 0.3f;
            hand.held += dt;
            if(out) {
                // A hold that did not last was at the edge of the arm's reach: the hand waits before it tries again
                if(hand.held < 0.25f) hand.wait = 0.6f;
                if(params.scramble && hand.wait <= 0.f && findHold(geo, shoulder, -1.f, print)) {
                    leave(print);
                } else {
                    hand.from_position = position;
                    hand.from_rotation = rotation;
                    hand.t = 0.f;
                    hand.contact = HandContact::Releasing;
                }
            } else if(state.moving && other.contact != HandContact::Reaching && hand.held > params.hand_hold) {
                // Behind its shoulder, or its arm well outside its limits, after it held for a while: it moves on to a hold
                // ahead, one hand at a time
                // ... where the arm is clearly better off
                glm::vec3 pole;
                const float over = elbowPole(geo, shoulder, position, rotation, hand.pole, pole);
                const bool behind = glm::dot(wrist - shoulder, dir) < -params.hand_behind;
                if((behind || over > params.hand_over) && findHold(geo, shoulder, 0.15f, print) && (behind || hold_over < over - 0.2f)) leave(print);
            }
        }

        if(hand.contact == HandContact::Reaching) {
            // The hold went out of reach while the hand was on its way: another one, or none
            glm::vec3 position;
            glm::quat rotation;
            footPose(geo.palm, hand.next, 0.f, 0, position, rotation);
            // ... (it must come down with some reach to spare, or it would let go at once)
            if(glm::length(position + rotation * model.anchor_child[geo.wrist] - shoulder) > 0.5f * (params.hand_plant_reach + params.hand_reach) * geo.reach) {
                if(params.scramble && findHold(geo, shoulder, -1.f, print)) {
                    leave(print);
                } else {
                    hand.from_position = hand.position;
                    hand.from_rotation = hand.rotation;
                    hand.current = hand.next;
                    hand.t = 0.f;
                    hand.wait = 0.6f;
                    hand.contact = HandContact::Releasing;
                }
            }
        }

        if(hand.contact == HandContact::Reaching) {
            hand.t = std::min(hand.t + dt / std::max(params.hand_time, 1e-3f), 1.f);
            glm::vec3 position;
            glm::quat rotation;
            footPose(geo.palm, hand.next, 0.f, 0, position, rotation);
            const float e = smooth(hand.t);
            hand.position = glm::mix(hand.from_position, position, e) + (hand.next.rotation * up) * (params.hand_lift * std::sin(pi * hand.t));
            hand.rotation = glm::normalize(glm::slerp(hand.from_rotation, rotation, e));
            if(hand.t >= 1.f) {
                hand.position = position;
                hand.rotation = rotation;
                hand.current = hand.next;
                hand.contact = HandContact::Planted;
                hand.planted_now = true;
                hand.deficit = 0.f;
                hand.held = 0.f;
            }
        } else if(hand.contact == HandContact::Planted) {
            footPose(geo.palm, hand.current, 0.f, 0, hand.position, hand.rotation);
        } else if(hand.contact == HandContact::Releasing) {
            hand.t = std::min(hand.t + dt / std::max(params.hand_time, 1e-3f), 1.f);
            const float e = smooth(hand.t);
            hand.position = glm::mix(hand.from_position, free_position, e) + (hand.current.rotation * up) * (params.hand_lift * std::sin(pi * hand.t));
            hand.rotation = glm::normalize(glm::slerp(hand.from_rotation, free_rotation, e));
            if(hand.t >= 1.f) hand.contact = HandContact::Free;
        }
        if(hand.contact == HandContact::Free) {
            hand.position = free_position;
            hand.rotation = free_rotation;
            hand.deficit = 0.f;
        }
        hand.grip = approach(hand.grip, hand.contact == HandContact::Planted ? 1.f : 0.f, dt / 0.25f);

        auto& chain = reg.get<IKChain>(hand.chain);
        chain.yield = params.yield_limits;
        auto& target = reg.get<IKTarget>(hand.target);
        target.enabled = hand.contact != HandContact::Free;
        if(!target.enabled) {
            hand.pole = glm::vec3(0.f);
            continue;
        }

        // Where the elbow points, turning there no faster than an arm does
        const glm::vec3 wrist = hand.position + hand.rotation * model.anchor_child[geo.wrist];
        glm::vec3 best(0.f);
        elbowPole(geo, shoulder, hand.position, hand.rotation, hand.pole, best);
        if(glm::length(hand.pole) > 0.5f) {
            glm::vec3 axis = wrist - shoulder;
            axis = glm::length(axis) > 1e-6f ? glm::normalize(axis) : -up;
            glm::vec3 pole = glm::mix(hand.pole, best, dt / (0.12f + dt));
            pole -= axis * glm::dot(pole, axis);
            hand.pole = glm::length(pole) > 1e-3f ? glm::normalize(pole) : best;
        } else {
            hand.pole = best;
        }

        target.position = wrist;
        target.rotation = hand.rotation;
        target.pole = hand.pole;
    }

}

// GaitBalance

void GaitBalanceSys::tick(double h) {

    OPTICK_EVENT();

    GaitRig g = gaitRig(reg);
    if(g.entity == entt::null || !g.state->feet_ready) return;
    const RigModel& model = *g.rig->model;
    const GaitParams& params = *g.params;
    GaitState& state = *g.state;
    RigPose& pose = *g.pose;
    GroundField& ground = groundField(reg);
    auto& balance = reg.get_or_emplace<BalanceState>(g.entity);
    const float dt = (float) h;
    const BodyDef& def = *model.body;

    // The hands that hold or reach: their arms are solved with the legs, and the trunk leans as far as they need
    struct Arm {
        HandState* hand;
        const IKChain* chain;
        IKGoal goal;
    };
    Arm arms[2];
    int arm_count = 0;
    const int spine_joint[2] = { model.jointIndex("Spine"), model.jointIndex("Spine2") };
    glm::quat spine_base[2] = { glm::quat(1.f, 0.f, 0.f, 0.f), glm::quat(1.f, 0.f, 0.f, 0.f) };
    glm::vec3 trunk_lean(0.f);
    glm::vec2 com_target = state.com_target;
    bool trunk = false;
    if(state.hands_ready && spine_joint[0] >= 0 && spine_joint[1] >= 0) {
        for(HandState& hand : state.hand) {
            if(hand.grip > 0.f) com_target += params.hand_support * hand.grip * (glm::vec2(hand.current.centre.x, hand.current.centre.z) - state.com_target);
            const auto& target = reg.get<IKTarget>(hand.target);
            if(!target.enabled) {
                hand.deficit = 0.f;
                continue;
            }
            arms[arm_count++] = Arm { &hand, &reg.get<IKChain>(hand.chain), IKGoal { target.position, target.rotation, target.use_rotation, target.pole } };
        }
        for(int k = 0; k < 2; k++) spine_base[k] = pose.joint[spine_joint[k]];
        // What the trunk leaned for the hands goes when no hand asks for it
        trunk_lean = state.trunk_reach * std::exp(-dt / std::max(params.trunk_return, 1e-3f));
        if(glm::length(trunk_lean) < 1e-5f) trunk_lean = glm::vec3(0.f);
        trunk = arm_count > 0 || trunk_lean != glm::vec3(0.f);
    }
    // The spine as GaitBase left it, turned by a world rotation vector, half on each of its joints
    auto leanTrunk = [&](RigPose& p, glm::vec3 lean) {
        for(int k = 0; k < 2; k++) p.joint[spine_joint[k]] = spine_base[k];
        const float angle = glm::length(lean);
        if(angle < 1e-6f) return;
        const glm::quat half = glm::angleAxis(0.5f * angle, lean / angle);
        SegmentPoses at;
        for(int k = 0; k < 2; k++) {
            model.forward(p, at);
            const JointDef& joint = def.joints[spine_joint[k]];
            p.joint[spine_joint[k]] = model.clamp(spine_joint[k], model.jointRotation(spine_joint[k], at.rotation[joint.parent], glm::normalize(half * at.rotation[joint.child])));
        }
    };

    const IKChain* chains[2];
    IKGoal goals[2];
    glm::vec3 hip_offset[2];
    for(int i = 0; i < 2; i++) {
        const FootState& f = state.foot[i];
        chains[i] = &reg.get<IKChain>(f.chain);
        const auto& target = reg.get<IKTarget>(f.target);
        goals[i] = IKGoal { target.position, target.rotation, target.use_rotation, target.pole };
        hip_offset[i] = pose.root_rotation * (model.anchor_parent[f.geometry.hip] - state.pivot);
    }

    // As high as both legs reach, no higher than the nominal height less a crouch held through the stride: what the
    // last step lacked in reach at its worst moment, so that the hips do not dip at that moment of every step
    const float nominal = state.hip_centre.y;
    if(state.foot[0].landed || state.foot[1].landed || !state.moving) {
        balance.wanted = balance.deficit > 0.f ? 1.05f * balance.deficit + 0.002f : 0.f;
        balance.deficit = 0.f;
    }
    balance.bias += (balance.wanted - balance.bias) * (dt / (params.crouch_time + dt));
    // Scrambling brings the hips down, but no nearer to an ankle than the knee folds: they are lifted for that, no
    // faster than hips move (a knee folds past its stop for a moment instead) and no higher than the legs reached
    // through the last step
    const bool folding = state.low > 0.f || balance.lift > 0.f;
    if(folding && (state.foot[0].landed || state.foot[1].landed || !state.moving)) {
        balance.lift_room = balance.lift_room_step;
        balance.lift_room_step = 10.f;
    }
    float limit = nominal, lift_now = 0.f, room_now = 10.f;
    auto height = [&](glm::vec3 hip) {
        float y = nominal + 1.f;
        for(int i = 0; i < 2; i++) {
            glm::vec3 joint = hip + hip_offset[i];
            float dx = joint.x - goals[i].position.x, dz = joint.z - goals[i].position.z;
            float rest = state.reach * state.reach - dx * dx - dz * dz;
            y = std::min(y, goals[i].position.y - hip_offset[i].y + (rest > 0.f ? std::sqrt(rest) : 0.f));
        }
        limit = y;
        y = std::min(y, nominal - balance.bias);
        if(folding) {
            float least = -1e9f;
            for(int i = 0; i < 2; i++) {
                glm::vec3 joint = hip + hip_offset[i];
                float dx = joint.x - goals[i].position.x, dz = joint.z - goals[i].position.z;
                float rest = state.fold * state.fold - dx * dx - dz * dz;
                if(rest > 0.f) least = std::max(least, goals[i].position.y - hip_offset[i].y + std::sqrt(rest));
            }
            lift_now = state.low > 0.f ? std::max(std::min(least, limit) - y, 0.f) : 0.f;
            room_now = std::max(limit - y, 0.f);
            // Reach comes first
            y = std::min(y + balance.lift, limit);
        }
        return y;
    };

    glm::vec3 hip = state.hip_centre;
    RigPose solved;
    SegmentPoses poses;
    glm::vec3 com(0.f);
    balance.iterations = 0;
    for(int k = 0; k < std::max(params.balance_iterations, 1); k++) {
        hip.y = height(hip);
        pose.root_position = hip - pose.root_rotation * state.pivot;
        if(trunk) {
            // The shoulders come within reach of the hands' targets: the trunk turns about the bottom of the spine
            leanTrunk(pose, trunk_lean);
            for(int pass = 0; pass < 6 && arm_count > 0; pass++) {
                model.forward(pose, poses);
                glm::vec3 shift(0.f);
                int lacking = 0;
                for(int a = 0; a < arm_count; a++) {
                    const HandGeometry& geo = arms[a].hand->geometry;
                    glm::vec3 d = arms[a].goal.position - model.anchor(poses, geo.shoulder);
                    const float l = glm::length(d), over = l - params.hand_reach * geo.reach;
                    if(over > 0.f) {
                        shift += d * (over / l);
                        lacking++;
                    }
                }
                if(lacking == 0) break;
                const glm::vec3 chest = 0.5f * (model.anchor(poses, state.hand[0].geometry.shoulder) + model.anchor(poses, state.hand[1].geometry.shoulder));
                const glm::vec3 lever = chest - model.anchor(poses, spine_joint[0]);
                // Each pass covers most of what is lacking: a sum over the hands, so that a second hand starting to lack changes nothing at once
                trunk_lean += glm::cross(lever, 0.6f * shift) / std::max(glm::dot(lever, lever), 1e-4f);
                const float angle = glm::length(trunk_lean);
                if(angle > params.trunk_reach) trunk_lean *= params.trunk_reach / angle;
                leanTrunk(pose, trunk_lean);
            }
        }
        solved = pose;
        for(int i = 0; i < 2; i++) solveChain(model, solved, chains[i]->joints, chains[i]->pole_rest, goals[i], chains[i]->limits, chains[i]->yield);
        for(int a = 0; a < arm_count; a++) solveChain(model, solved, arms[a].chain->joints, arms[a].chain->pole_rest, arms[a].goal, arms[a].chain->limits, arms[a].chain->yield);
        model.forward(solved, poses);
        com = model.centerOfMass(poses);
        glm::vec2 error(com_target.x - com.x, com_target.y - com.z);
        balance.error = glm::length(error);
        balance.iterations = k + 1;
        if(balance.error < params.balance_tolerance || k + 1 >= params.balance_iterations) break;
        hip.x += error.x;
        hip.z += error.y;
    }
    balance.crouch = nominal - hip.y;
    balance.offset = hip - state.hip_centre;
    if(folding) {
        balance.lift_room_step = std::min(balance.lift_room_step, room_now);
        balance.lift = approach(balance.lift, std::min(lift_now, balance.lift_room), 0.3f * dt);
    }
    if(trunk) {
        state.trunk_reach = trunk_lean;
        for(int a = 0; a < arm_count; a++) {
            const HandGeometry& geo = arms[a].hand->geometry;
            HandState& hand = *arms[a].hand;
            hand.deficit = std::max(glm::length(arms[a].goal.position - model.anchor(poses, geo.shoulder)) - params.hand_reach * geo.reach, 0.f);
            // Out of reach whatever the trunk does: the palm lets go now
            if(hand.contact == HandContact::Planted && hand.deficit > 5e-3f) {
                hand.from_position = hand.position;
                hand.from_rotation = hand.rotation;
                hand.t = 0.f;
                if(hand.held < 0.25f) hand.wait = 0.6f;
                hand.contact = HandContact::Releasing;
            }
        }
    }
    balance.deficit = std::max(balance.deficit, nominal - limit);
    balance.wanted = std::max(balance.wanted, balance.deficit);

    // The centre of mass and where its velocity carries it
    const glm::vec3 dir = state.direction(), right = state.right();
    if(balance.valid && dt > 0.f) {
        glm::vec3 velocity = (com - balance.com) / dt;
        balance.velocity += (dt / (params.velocity_smoothing + dt)) * (velocity - balance.velocity);
    } else {
        balance.velocity = dir * state.speed;
    }
    balance.com = com;
    balance.ground = ground.height(com.x, com.z, com.y, state.centre.y);
    balance.xcom = glm::vec3(com.x + balance.velocity.x / state.omega, balance.ground, com.z + balance.velocity.z / state.omega);
    balance.lead = glm::dot(balance.xcom - state.centre, dir);
    balance.valid = true;

    // Between the feet: the hull of the soles' last and next prints
    std::vector<glm::vec2> points;
    for(const FootState& f : state.foot) {
        for(int c = 0; c < 4; c++) {
            glm::vec3 p = soleCorner(f.geometry, f.current, c);
            points.emplace_back(p.x, p.z);
            if(!f.stance && f.planned) {
                p = soleCorner(f.geometry, f.next, c);
                points.emplace_back(p.x, p.z);
            }
        }
    }
    if(state.hands_ready) {
        for(const HandState& hand : state.hand) {
            if(hand.contact != HandContact::Planted) continue;
            for(int c = 0; c < 4; c++) {
                glm::vec3 p = soleCorner(hand.geometry.palm, hand.current, c);
                points.emplace_back(p.x, p.z);
            }
        }
    }
    balance.support = convexHull(points);
    balance.outside = outsideDistance(balance.support, glm::vec2(com.x, com.z));

    balance.lateral_margin = 1.f;
    balance.captured = false;
    for(const FootState& f : state.foot) {
        if(!f.stance) continue;
        const FootGeometry& geo = f.geometry;
        float edge = glm::dot(f.current.centre - state.centre, right) + geo.side * geo.half_width;
        balance.lateral_margin = std::min(balance.lateral_margin, geo.side * (edge - glm::dot(balance.xcom - state.centre, right)));
        if(f.landed) {
            glm::vec3 toes = f.current.rotation * glm::vec3(0.f, 0.f, -1.f);
            glm::vec3 heel = f.current.centre - toes * geo.half_length;
            balance.captured = true;
            // The strike was between two ticks: the body has walked on since
            float since = state.footPhase(&f == &state.foot[1] ? 1 : 0) * state.stride_time;
            balance.capture_error = glm::dot(heel - balance.xcom, dir) + state.capture_offset + state.speed * since;
        }
    }

}

// GaitChecks

void GaitChecksSys::tick(double h) {

    OPTICK_EVENT();

    GaitRig g = gaitRig(reg);
    auto* bot = reg.ctx().find<HumanBot>();
    if(g.entity == entt::null || !g.state->feet_ready || !bot || !bot->built) return;
    const RigModel& model = *g.rig->model;
    const BodyDef& def = *model.body;
    GaitState& state = *g.state;
    GroundField& ground = groundField(reg);
    auto& report = reg.get_or_emplace<GaitReport>(g.entity);
    const auto* balance = reg.try_get<BalanceState>(g.entity);
    const RagdollState& s = bot->state;

    report.contact_error = report.penetration = report.slip = report.sole_angle = 0.f;
    float gap = 0.f;
    for(int i = 0; i < 2; i++) {
        const FootState& f = state.foot[i];
        const FootGeometry& geo = f.geometry;
        glm::vec3 position = s.position[geo.segment];
        glm::quat rotation = s.rotation[geo.segment];
        if(f.landed) report.steps++;
        for(int c = 0; c < 4; c++) {
            glm::vec3 corner = geo.corner(c, position, rotation);
            bool heel = c < 2;
            bool planted = f.contact == FootContact::Flat || (f.contact == FootContact::Heel && heel) || (f.contact == FootContact::Toe && !heel);
            if(auto hit = ground.below(glm::vec3(corner.x, corner.y + 2.f, corner.z), 8.f)) {
                float distance = (corner.y - hit->point.y) * hit->normal.y;
                report.penetration = std::max(report.penetration, -distance);
                if(planted) report.contact_error = std::max(report.contact_error, std::abs(distance));
            }
            if(planted) {
                if(!report.is_planted[i][c] || f.landed) {
                    report.planted[i][c] = corner;
                    report.is_planted[i][c] = true;
                }
                report.slip = std::max(report.slip, glm::length(corner - report.planted[i][c]));
            } else {
                report.is_planted[i][c] = false;
            }
        }
        if(f.contact == FootContact::Flat) {
            glm::vec3 sole = rotation * (geo.sole_rotation * up), print = f.current.rotation * up;
            report.sole_angle = std::max(report.sole_angle, std::acos(std::min(1.f, glm::dot(sole, print))));
        }
        if(f.stance) gap = std::max(gap, f.current.gap);
    }
    if(state.ground_version != ground.version) report.is_planted = {};

    report.limit_excess = report.body_limit_excess = 0.f;
    float worst = 0.f;
    for(int j = 0; j < model.joints; j++) {
        const JointDef& joint = def.joints[j];
        glm::quat q = model.jointRotation(j, s.rotation[joint.parent], s.rotation[joint.child]);
        report.limit_excess = std::max(report.limit_excess, RigModel::excess(model.limits[j], q));
        float e = RigModel::excess(model.body_limits[j], q);
        report.body_limit_excess = std::max(report.body_limit_excess, e);
        if(e > worst) {
            worst = e;
            if(e > report.max_body_limit_excess) report.worst_joint = j;
        }
    }

    report.ik_position = report.ik_rotation = report.ik_limit = report.swing_ik_position = report.swing_ik_rotation = 0.f;
    float relief = 0.f;
    for(const FootState& f : state.foot) {
        const auto& chain = reg.get<IKChain>(f.chain);
        float& position = f.stance ? report.ik_position : report.swing_ik_position;
        float& rotation = f.stance ? report.ik_rotation : report.swing_ik_rotation;
        position = std::max(position, chain.position_error);
        rotation = std::max(rotation, chain.rotation_error);
        report.ik_limit = std::max(report.ik_limit, chain.limit_excess);
        if(f.stance) relief = std::max(relief, f.relief);
    }
    if(balance && balance->captured) report.capture_error = balance->capture_error;

    // Hands
    const bool settled = state.time >= report.settle;
    report.hand_contact_error = report.hand_penetration = report.hand_slip = report.hand_ik_position = report.hand_ik_rotation = 0.f;
    if(state.hands_ready) {
        for(int i = 0; i < 2; i++) {
            const HandState& hand = state.hand[i];
            const FootGeometry& palm = hand.geometry.palm;
            const bool planted = hand.contact == HandContact::Planted;
            for(int c = 0; c < 4; c++) {
                glm::vec3 corner = palm.corner(c, s.position[palm.segment], s.rotation[palm.segment]);
                if(!planted) {
                    report.hand_is_planted[i][c] = false;
                    continue;
                }
                if(auto hit = ground.below(glm::vec3(corner.x, corner.y + 1.f, corner.z), 4.f)) {
                    float distance = (corner.y - hit->point.y) * hit->normal.y;
                    report.hand_penetration = std::max(report.hand_penetration, -distance);
                    report.hand_contact_error = std::max(report.hand_contact_error, std::abs(distance));
                }
                if(!report.hand_is_planted[i][c] || hand.planted_now) {
                    report.hand_planted[i][c] = corner;
                    report.hand_is_planted[i][c] = true;
                }
                report.hand_slip = std::max(report.hand_slip, glm::length(corner - report.hand_planted[i][c]));
            }
            const auto& chain = reg.get<IKChain>(hand.chain);
            if(planted) {
                report.hand_ik_position = std::max(report.hand_ik_position, chain.position_error);
                report.hand_ik_rotation = std::max(report.hand_ik_rotation, chain.rotation_error);
                if(settled) report.hand_ticks[i]++;
            }
            if(settled && hand.planted_now) report.hand_holds[i]++;
            if(settled && chain.solved) report.max_arm_limit = std::max(report.max_arm_limit, chain.limit_excess);
        }
        if(state.ground_version != ground.version) report.hand_is_planted = {};
    }

    report.ticks++;
    if(state.time < report.settle) return;
    report.max_hand_contact_error = std::max(report.max_hand_contact_error, report.hand_contact_error);
    report.max_hand_penetration = std::max(report.max_hand_penetration, report.hand_penetration);
    report.max_hand_slip = std::max(report.max_hand_slip, report.hand_slip);
    report.max_hand_ik_position = std::max(report.max_hand_ik_position, report.hand_ik_position);
    report.max_hand_ik_rotation = std::max(report.max_hand_ik_rotation, report.hand_ik_rotation);
    report.max_low = std::max(report.max_low, state.low);
    if(balance && balance->valid) report.min_hip_height = std::min(report.min_hip_height, state.hip_centre.y + balance->offset.y - state.centre.y);
    if(report.settle_time < 0. && balance) {
        report.settle_time = state.time;
        report.settle_com = balance->com;
    }
    report.max_contact_error = std::max(report.max_contact_error, report.contact_error);
    report.max_penetration = std::max(report.max_penetration, report.penetration);
    report.max_slip = std::max(report.max_slip, report.slip);
    report.max_sole_angle = std::max(report.max_sole_angle, report.sole_angle);
    report.max_limit_excess = std::max(report.max_limit_excess, report.limit_excess);
    report.max_body_limit_excess = std::max(report.max_body_limit_excess, report.body_limit_excess);
    report.max_ik_position = std::max(report.max_ik_position, report.ik_position);
    report.max_ik_rotation = std::max(report.max_ik_rotation, report.ik_rotation);
    report.max_ik_limit = std::max(report.max_ik_limit, report.ik_limit);
    report.max_swing_ik_position = std::max(report.max_swing_ik_position, report.swing_ik_position);
    report.max_swing_ik_rotation = std::max(report.max_swing_ik_rotation, report.swing_ik_rotation);
    report.max_relief = std::max(report.max_relief, relief);
    for(const FootState& f : state.foot) if(reg.get<IKChain>(f.chain).yielded) { report.yielded++; break; }
    report.max_gap = std::max(report.max_gap, gap);
    if(balance) {
        if(balance->captured) report.max_capture_error = std::max(report.max_capture_error, std::abs(balance->capture_error));
        report.max_com_error = std::max(report.max_com_error, balance->error);
        report.max_outside = std::max(report.max_outside, balance->outside);
        report.min_lateral_margin = std::min(report.min_lateral_margin, balance->lateral_margin);
        double elapsed = state.time - report.settle_time;
        if(elapsed > 0.) report.mean_speed = glm::length(horizontal(balance->com - report.settle_com)) / (float) elapsed;
    }

}

void dy::registerGait() {

    registerComponent("GaitCommand", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            GaitCommand c;
            JsonFields f(j);
            float heading = 0.f;
            f.read("speed", c.speed);
            f.read("heading", heading);
            f.read("start", c.start);
            f.finish();
            if(c.speed < 0.f) throw ComponentError("/speed", "must not be negative");
            c.heading = glm::radians(heading);
            reg.emplace_or_replace<GaitCommand>(e, c);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& c = reg.get<GaitCommand>(e);
            return nlohmann::json { { "speed", c.speed }, { "heading", glm::degrees(c.heading) }, { "start", { c.start.x, c.start.y } } };
        }
    });

    registerComponent("GaitParams", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            GaitParams p;
            JsonFields f(j);
            float strike = glm::degrees(p.strike_angle), toe = glm::degrees(p.toe_angle);
            float roll = glm::degrees(p.ankle_roll_extra), yaw = glm::degrees(p.ankle_yaw_extra);
            float from = glm::degrees(p.scramble_from), full = glm::degrees(p.scramble_full), lean = glm::degrees(p.scramble_lean), trunk = glm::degrees(p.trunk_reach);
            f.read("step_factor", p.step_factor);
            f.read("step_exponent", p.step_exponent);
            f.read("max_stride_time", p.max_stride_time);
            f.read("stance_share", p.stance_share);
            f.read("step_width", p.step_width);
            f.read("strike_angle", strike);
            f.read("toe_angle", toe);
            f.read("swing_lift", p.swing_lift);
            f.read("swing_clearance", p.swing_clearance);
            f.read("leg_extension", p.leg_extension);
            f.read("hip_height", p.hip_height);
            f.read("lean_slope_gain", p.lean_slope_gain);
            f.read("acceleration", p.acceleration);
            f.read("turn_rate", p.turn_rate);
            f.read("slope_slowdown", p.slope_slowdown);
            f.read("knee_out", p.knee_out);
            f.read("yield_limits", p.yield_limits);
            f.read("slope_speed_floor", p.slope_speed_floor);
            f.read("step_rise", p.step_rise);
            f.read("ankle_roll_extra", roll);
            f.read("ankle_yaw_extra", yaw);
            f.read("scramble", p.scramble);
            f.read("scramble_from", from);
            f.read("scramble_full", full);
            f.read("scramble_crouch", p.scramble_crouch);
            f.read("scramble_lean", lean);
            f.read("scramble_shoulder", p.scramble_shoulder);
            f.read("hand_time", p.hand_time);
            f.read("hand_support", p.hand_support);
            f.read("trunk_reach", trunk);
            f.finish();
            p.scramble_from = glm::radians(from);
            p.scramble_full = glm::radians(full);
            p.scramble_lean = glm::radians(lean);
            p.trunk_reach = glm::radians(trunk);
            p.strike_angle = glm::radians(strike);
            p.toe_angle = glm::radians(toe);
            p.ankle_roll_extra = glm::radians(roll);
            p.ankle_yaw_extra = glm::radians(yaw);
            reg.emplace_or_replace<GaitParams>(e, p);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& p = reg.get<GaitParams>(e);
            return nlohmann::json { { "step_factor", p.step_factor }, { "step_exponent", p.step_exponent }, { "max_stride_time", p.max_stride_time },
                { "stance_share", p.stance_share }, { "step_width", p.step_width }, { "strike_angle", glm::degrees(p.strike_angle) },
                { "toe_angle", glm::degrees(p.toe_angle) }, { "swing_lift", p.swing_lift }, { "swing_clearance", p.swing_clearance },
                { "leg_extension", p.leg_extension }, { "hip_height", p.hip_height }, { "lean_slope_gain", p.lean_slope_gain },
                { "acceleration", p.acceleration }, { "turn_rate", p.turn_rate }, { "slope_slowdown", p.slope_slowdown }, { "slope_speed_floor", p.slope_speed_floor }, { "step_rise", p.step_rise }, { "knee_out", p.knee_out }, { "yield_limits", p.yield_limits },
                { "ankle_roll_extra", glm::degrees(p.ankle_roll_extra) }, { "ankle_yaw_extra", glm::degrees(p.ankle_yaw_extra) },
                { "scramble", p.scramble }, { "scramble_from", glm::degrees(p.scramble_from) }, { "scramble_full", glm::degrees(p.scramble_full) },
                { "scramble_crouch", p.scramble_crouch }, { "scramble_lean", glm::degrees(p.scramble_lean) }, { "scramble_shoulder", p.scramble_shoulder },
                { "hand_time", p.hand_time }, { "hand_support", p.hand_support }, { "trunk_reach", glm::degrees(p.trunk_reach) } };
        }
    });

    registerSystem("GaitBase", systemInfo<GaitBaseSys>(SystemScope::World));
    registerSystem("GaitFootIK", systemInfo<GaitFootIKSys>(SystemScope::World));
    registerSystem("GaitHandIK", systemInfo<GaitHandIKSys>(SystemScope::World));
    registerSystem("GaitBalance", systemInfo<GaitBalanceSys>(SystemScope::World));
    registerSystem("GaitChecks", systemInfo<GaitChecksSys>(SystemScope::World));

}
