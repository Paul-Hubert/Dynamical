#ifndef DY_GAIT_H
#define DY_GAIT_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <entt/entt.hpp>

#include <array>
#include <vector>

#include "ik.h"
#include "logic/systems/system.h"
#include "rig_pose.h"

namespace dy {

class GroundField;

// The kinematic walk (docs/physical-agents/ik-gait.md): a procedural gait on any ground, legs by inverse kinematics.
// Fixed lane order: GaitBase, GaitFootIK, GaitBalance, Fabrik, RigPoseApply, PhysicsStep, BodySync, GaitChecks.
// Every component here sits on the rig entity (the one with HumanBotSpawn)

// What the walker is asked to do
struct GaitCommand {
    float speed = 1.36f;                        // m/s, horizontal
    float heading = 0.f;                        // radians about +y; 0 walks toward -z
    glm::vec2 start = glm::vec2(0.f);           // x, z where it stands at the first tick
};

// The gait's laws and shapes. Angles in radians, lengths in metres
struct GaitParams {
    // Step length / leg length = step_factor * (speed / sqrt(g * leg length)) ^ step_exponent; the stride never
    // lasts longer than max_stride_time (slow walking shortens the step instead)
    float step_factor = 1.11f, step_exponent = 0.42f;
    float max_stride_time = 1.6f;
    // Share of the stride a foot is on the ground: stance_share at reference_speed, changing with speed
    float stance_share = 0.62f, stance_share_rate = -0.055f, reference_speed = 1.36f;     // rate per m/s
    float step_width = 0.14f;                   // between the sole centres' paths
    // Stance, as shares of its duration: heel rocker until flat_start, flat until heel_off, then the toe rocker
    float flat_start = 0.16f, heel_off = 0.5f;
    float strike_angle = 0.26f, toe_angle = 1.05f;      // sole pitch at heel strike and at toe-off, at reference_speed
    float swing_lift = 0.03f, swing_clearance = 0.025f; // extra ankle arc; least gap between the sole and the ground
    float swing_level = 0.6f;                   // share of the swing by which the sole has turned from its toe-off pitch to its landing pitch
    float replan_until = 0.6f;                  // share of the swing during which the landing is still planned
    float max_heel_rise = 1.4f;                 // the most the toe rocker pitches the sole, radians
    float heel_drop_rate = 2.5f;                // rad/s: how fast a heel raised for reach or for the ankle comes back down
    float ankle_margin = 0.035f;                // the heel rises when the ankle is this close to its dorsiflexion stop
    float knee_follow = 0.5f;                   // in a turn the knee points this share of the way from the pelvis' forward to the foot's
    float knee_out = 0.5f;                      // the knee points this share of the way from the walker's forward to the foot's toes
    // Added to the ankles' swing limits for the IK only, about the foot's long axis (roll) and the sole's normal
    // (yaw). 0: the body file's, which were widened for 15 degree side slopes (assets.md)
    float ankle_roll_extra = 0.f, ankle_yaw_extra = 0.f;
    // A planted foot comes first: where a leg cannot put its foot on its print inside the joint limits, the limits
    // give way (steep ground). false: the limits always hold and the foot misses its print
    bool yield_limits = true;
    // Pelvis and trunk
    float leg_extension = 0.992f;               // most of the thigh + shank length a leg stretches to (the knee keeps 14 degrees)
    float hip_height = 1.f;                     // the hips' height over the ankles in mid stance, as a share of that reach
    float bob = 0.035f;                         // the hips sink this much at each double support, at reference_speed
    float crouch_time = 0.35f;                  // seconds: how fast the hips settle to the crouch the last step needed
    float pelvis_yaw = 0.07f, pelvis_list = 0.035f;     // amplitudes at reference_speed
    float counter_rotation = 0.6f;              // shoulders turn against the pelvis by this share of its yaw
    float lean = 0.035f, lean_slope_gain = 0.5f;        // trunk pitch at reference_speed; per radian of slope along the heading
    float arm_swing = 0.32f, arm_bias = 0.05f, elbow = 0.17f, elbow_swing = 0.2f;
    // Command following
    float acceleration = 1.f, turn_rate = 0.7f, min_speed = 0.05f;
    float slope_slowdown = 1.5f;                // the speed walked is the command times exp(-slope_slowdown * |tan slope|),
    float slope_speed_floor = 0.25f;            // ... and no less than this share of it, however steep,
    float step_rise = 0.35f;                    // ... but slow enough that a step climbs or drops no more than this, m (0: no such cap)
    float look_ahead = 0.8f;                    // m ahead of the walker where a steeper slope already slows it
    float braking = 2.5f;                       // m/s2, slowing down (acceleration is for speeding up)
    float max_print_rise = 0.5f;                // m between a foot's landing and the other foot's print: the step shortens to keep it
    float max_print_tilt = 1.31f;               // radians: steeper ground than this (75 degrees) is stood on at this tilt
    // Balance
    int balance_iterations = 8;
    float balance_tolerance = 3e-4f;
    float velocity_smoothing = 0.08f;           // seconds, the centre of mass velocity filter
};

// A planted sole: the centre of its bottom face and its frame (x right, y the ground normal, -z toward the toes)
struct Footprint {
    glm::vec3 centre = glm::vec3(0.f);
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    float gap = 0.f;                            // how far the ground under the sole is from a plane, m
};

enum class FootContact : uint8_t { None, Heel, Flat, Toe };

// A foot of the body file: its box, seen as a sole
struct FootGeometry {
    int segment = -1;
    int hip = -1, knee = -1, ankle = -1;        // joints
    glm::quat sole_rotation = glm::quat(1.f, 0.f, 0.f, 0.f);    // the sole frame in the foot segment's frame
    glm::vec3 sole_centre = glm::vec3(0.f);     // centre of the bottom face, foot frame
    float half_width = 0.05f, half_length = 0.14f;
    float toe_out = 0.f;                        // yaw of the sole at the rest pose
    glm::vec3 pole = glm::vec3(0.f, 0.f, -1.f); // the rest pose's forward, foot frame: where the knee points
    float side = -1.f;                          // -1 left, +1 right

    // Sole corner i (0 heel left, 1 heel right, 2 toe left, 3 toe right) of a foot segment at this pose
    glm::vec3 corner(int i, glm::vec3 position, glm::quat rotation) const;
};

struct FootState {
    FootGeometry geometry;
    entt::entity chain = entt::null, target = entt::null;

    bool stance = true;
    FootContact contact = FootContact::Flat;
    Footprint current;                          // where it stands, or where it left
    Footprint next;                             // where it will land
    bool planned = false, toe_latched = false, landed = false;  // landed: heel strike on this tick
    float strike = 0.f, toe = 0.f;              // this step's rocker angles
    float pitch = 0.f;                          // the sole on its print: positive on the heel edge, negative on the toe edge
    float relief = 0.f;                         // of that, the heel rise the ankle's stop asked for
    float reach_relief = 0.f;                   // ... and the heel rise that keeps the ankle within the leg's reach
    float extra = 0.f;                          // heel rise over what the stride asks, held so that the heel comes down gently
    float lift = 0.f;
    glm::vec3 from_position = glm::vec3(0.f);   // the foot segment at toe-off
    glm::quat from_rotation = glm::quat(1.f, 0.f, 0.f, 0.f);

    glm::vec3 position = glm::vec3(0.f);        // the foot segment this tick
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::vec3 pole = glm::vec3(0.f, 0.f, -1.f);
};

struct GaitState {
    bool started = false, feet_ready = false;
    double time = 0.;
    float phase = 0.f;                          // of the stride, 0 at the left heel strike
    float speed = 0.f, heading = 0.f;           // the command, followed at the allowed rates and slowed by the slope
    float wanted_speed = 0.f;                   // what the speed is heading for
    bool moving = false;
    glm::vec3 centre = glm::vec3(0.f);          // the walker's reference point on the ground
    uint64_t ground_version = 0;

    // This tick's gait laws
    float leg_length = 0.96f, reach = 0.84f;    // hip to sole; the longest hip to ankle distance
    float step_length = 0.f, stride_time = 1.f, stance_share = 0.62f, amplitude = 0.f;
    float omega = 3.1f;                         // sqrt(g / height of the centre of mass)
    float capture_offset = 0.f;                 // the heel lands this far behind the extrapolated centre of mass
    float sway = 0.f;                           // lateral offset of the centre of mass target, along the walker's right
    float slope_pitch = 0.f, slope_roll = 0.f;  // of the ground along and across the heading (uphill, right side up)
    float steepness = 0.f;                      // tan of the steepest slope under or just ahead of the walker
    float lean = 0.f;
    glm::vec3 hip_centre = glm::vec3(0.f);      // between the hip joints, before GaitBalance
    glm::vec3 pivot = glm::vec3(0.f);           // the same point in the pelvis frame
    glm::vec2 com_target = glm::vec2(0.f);      // x, z

    std::array<FootState, 2> foot;              // left, right

    float footPhase(int i) const { float p = phase + (i ? 0.5f : 0.f); return p - std::floor(p); }
    glm::vec3 direction() const { return glm::vec3(-std::sin(heading), 0.f, -std::cos(heading)); }
    glm::vec3 right() const { return glm::vec3(std::cos(heading), 0.f, -std::sin(heading)); }
};

// Written by GaitBalance
struct BalanceState {
    bool valid = false;
    glm::vec3 com = glm::vec3(0.f), velocity = glm::vec3(0.f);
    glm::vec3 xcom = glm::vec3(0.f);            // extrapolated centre of mass on the ground: com + velocity / omega
    float ground = 0.f;                         // height of the ground under the centre of mass
    float lead = 0.f;                           // m the extrapolated centre of mass is ahead of the reference point
    float error = 0.f;                          // m left between the centre of mass and its target
    int iterations = 0;
    float crouch = 0.f;                         // m the hips sit under their nominal height
    float bias = 0.f;                           // m of that held through the stride, so the hips move smoothly
    float deficit = 0.f, wanted = 0.f;          // the most reach this step lacked so far; what bias is settling to
    glm::vec3 offset = glm::vec3(0.f);          // where the hips ended, from where GaitBase put them
    std::vector<glm::vec2> support;             // convex hull of the soles' last and next prints, x z
    float outside = 0.f;                        // m the centre of mass is outside it
    float lateral_margin = 0.f;                 // m from the extrapolated centre of mass to the stance foot's outer edge
    bool captured = false;                      // a heel struck on this tick
    float capture_error = 0.f;                  // m along the heading between that heel and xcom - capture_offset
};

// Worst values since `settle` seconds (GaitChecks), and this tick's
struct GaitReport {
    double settle = 2.;
    int ticks = 0, steps = 0;
    float contact_error = 0.f, max_contact_error = 0.f;     // m between a planted sole point and the ground
    float penetration = 0.f, max_penetration = 0.f;         // m any sole point is under the ground
    float slip = 0.f, max_slip = 0.f;                       // m a planted sole point moved since it was planted
    float sole_angle = 0.f, max_sole_angle = 0.f;           // radians between a flat sole and its footprint
    float limit_excess = 0.f, max_limit_excess = 0.f;       // radians outside the IK limits (the bodies' pose)
    float body_limit_excess = 0.f, max_body_limit_excess = 0.f;   // ... outside the body file's
    int worst_joint = -1;                                   // the joint furthest outside the body file's limits
    float ik_position = 0.f, max_ik_position = 0.f;         // m and radians, the residual of the legs in stance
    float ik_rotation = 0.f, max_ik_rotation = 0.f;
    float swing_ik_position = 0.f, max_swing_ik_position = 0.f;     // ... of the legs in swing
    float swing_ik_rotation = 0.f, max_swing_ik_rotation = 0.f;
    float max_relief = 0.f;                                 // radians, the most heel rise an ankle stop asked for
    float ik_limit = 0.f, max_ik_limit = 0.f;               // radians the IK limits took away, or would have where they gave way
    int yielded = 0;                                        // ticks on which a leg's limits gave way, since settle
    float capture_error = 0.f, max_capture_error = 0.f;     // m between a heel strike and where the rule puts it
    float max_com_error = 0.f, max_outside = 0.f;
    float min_lateral_margin = 1.f;
    float max_gap = 0.f;                                    // m, the least flat ground a foot stood on
    float mean_speed = 0.f;                                 // m/s of the centre of mass since settle
    std::array<std::array<glm::vec3, 4>, 2> planted {};     // where each sole point was planted
    std::array<std::array<bool, 4>, 2> is_planted {};
    glm::vec3 settle_com = glm::vec3(0.f);
    double settle_time = -1.;
};

// The gait's clock and base animation: follows the command, advances the phase and the reference point, and writes
// the pelvis, the trunk, the head and the arms into the RigPose (the legs are left to the IK)
class GaitBaseSys : public System {
public:
    GaitBaseSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "GaitBase"; }
    void tick(double h) override;
};

// Plants the feet on the ground field: footsteps where the balance rule wants them, heel strike, flat stance and
// toe-off on prints fixed in the world, swing arcs over the ground. Creates and updates the legs' IKChain and IKTarget
class GaitFootIKSys : public System {
public:
    GaitFootIKSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "GaitFootIK"; }
    void tick(double h) override;
};

// Places the pelvis: as high as the legs reach, and where the centre of mass of the solved pose sits on its target
class GaitBalanceSys : public System {
public:
    GaitBalanceSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "GaitBalance"; }
    void tick(double h) override;
};

// After BodySync: measures the bodies against the ground, the footprints and the limits (GaitReport)
class GaitChecksSys : public System {
public:
    GaitChecksSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "GaitChecks"; }
    void tick(double h) override;
};

// The sole of a foot segment of the body
FootGeometry footGeometry(const RigModel& model, bool right);
// A sole of this geometry on the ground around (x, z), its toes toward `yaw`
// (no steeper than max_tilt, radians from level)
Footprint fitFootprint(const GroundField& ground, const FootGeometry& foot, glm::vec2 centre, float yaw, float from_y, float max_tilt = 1.31f);
// The foot segment standing on a print, pitched about its heel edge (pivot +1, toes up for a positive pitch) or
// its toe edge (pivot -1)
void footPose(const FootGeometry& foot, const Footprint& print, float pitch, int pivot, glm::vec3& position, glm::quat& rotation);

void registerGait();

}

#endif
