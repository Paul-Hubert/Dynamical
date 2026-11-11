#ifndef DY_ANIMAL_GAIT_H
#define DY_ANIMAL_GAIT_H

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "logic/animals/animal.h"
#include "logic/components/transform.h"
#include "logic/systems/system.h"

namespace dy {

class GroundField;

// A leg, as a walk clip shows it : the chain of joints from the hip to the joint that carries the weight, and when
// that joint is on the ground over a cycle
struct GaitLeg {
    std::vector<int> chain;             // joints, the hip first, the foot last
    std::vector<std::vector<int>> under; // per joint of the chain : itself and every joint under it
    int attach = -1;                    // the joint the hip hangs from (pelvis, chest), -1 : a root
    float touch = 0.f, lift = 0.f;      // phases of the cycle, in [0, 1), where the foot lands and where it leaves
    float duty = 0.5f;                  // part of the cycle it is on the ground
    float sole = 0.f;                   // height of the foot joint over the ground while it is planted
    float apex = 0.f;                   // how high the clip lifts it over that
    float reach = 0.f;                  // length of the chain
    float travel = 0.f;                 // how far back the foot goes under the body while it is planted
    glm::vec3 stand = glm::vec3(0.f);   // the foot joint in the middle of its stance, model space
    std::vector<float> rise;            // 16 samples of the swing : height over the planted height, as a part of apex
    bool front = false, left = false;
};

// What a walk (or trot, or run) clip is made of, for the procedural walk : found by looking at the clip, with no
// knowledge of the animal (two legs, four, a bird, a man)
struct GaitModel {
    bool valid = false;
    std::string problem;                // why it is not
    int clip = -1;
    float period = 1.f;                 // seconds of a cycle
    float speed = 1.f;                  // m/s of the clip : what its planted feet cover, the clip's own figure without that
    float ground = 0.f;                 // height of the ground in model space
    std::vector<GaitLeg> legs;
    float hind = 0.f, fore = 0.f;       // z of the hips of the hind legs and of the fore legs (the same for a biped)
    float height = 1.f;                 // of the animal
    std::vector<int> anchor;            // per joint : the hip of the leg it belongs to, itself when it is of the body
};

// Looks at a clip of a species. samples : poses of the cycle looked at
GaitModel analyseGait(const AnimalSpecies& species, int clip, int samples = 64);

// Everything the procedural walk can be asked : a point of this space is one way of walking. Demonstrations for
// imitation learning are drawn from distributions over it (GaitRanges)
struct GaitStyle {
    float stride = 1.f;             // length of the steps against the clip's : longer steps at the same speed are slower ones
    float step_height = 1.f;        // how high the feet are lifted against the clip
    float clearance = 0.f;          // metres the feet are lifted over the ground at least, whatever the clip does
    float soft_depth = 0.f;         // metres the feet sink in the ground (mud, snow, sand)
    float sink_time = 0.12f;        // seconds a foot takes to sink once it has landed
    float exit_vertical = 0.8f;     // 0 : the foot leaves the soft ground along its swing, 1 : it is pulled straight up first
    float entry_vertical = 0.5f;    // the same as it lands
    float exit_part = 0.3f;         // part of the swing spent getting out of the ground, when it is as deep as the foot lifts
    float body_sink = 0.7f;         // part of soft_depth the body comes down by
    float crouch = 0.f;             // metres the body is lowered by
    float spread = 0.f;             // metres the feet are set further out to the sides
    float bend = 1.f;               // how much the body follows the curve of its path (1 : the arc of the path)
    float max_bend = 40.f;          // degrees between the hind and the fore legs at most
    float head_lead = 0.6f;         // how much more the neck and the head turn into the curve
    float bank = 0.5f;              // lean into the turn : 1 is the lean that balances it
    float turn_step = 1.f;          // how much the feet are set down along the curve, 0 : as if the path were straight
    float ik = 1.f;                 // 0 : the clip as it is, feet sliding; 1 : feet planted

    static GaitStyle parse(const nlohmann::json& j, GaitStyle base = GaitStyle());
    nlohmann::json json() const;
    // Names and values, in a fixed order : what a demonstration is labelled with
    static const std::vector<std::string>& names();
    std::vector<float> values() const;
};

// On an animal : it walks procedurally whenever it plays one of its gait clips. AnimalGaitSys reads how the entity's
// Transform moves and makes the pose : the clip in place, bent along the curve of the path, feet planted on the
// ground through the legs' inverse kinematics.
struct AnimalGait {
    GaitStyle style;

    struct Foot {
        bool planted = false;
        bool placed = false;
        glm::vec3 position = glm::vec3(0.f);    // world : where it is planted, or where it left the ground
        glm::vec3 target = glm::vec3(0.f);      // world : where it will land
        glm::vec3 now = glm::vec3(0.f);         // world : where it is, the joint itself
        float ground = 0.f;                     // height of the ground under it
        float since = 0.f;                      // seconds since it landed
    };
    std::vector<Foot> feet;

    // Written by the system
    int model = -1;                 // index of the clip the pose came from, -1 : no gait this frame
    float weight = 0.f;             // 0 : the clip, 1 : the procedural walk
    float phase = 0.f;
    float period = 0.f;             // seconds of a cycle now : the clip's, stretched to the speed
    float speed = 0.f;              // m/s along the heading
    float turn = 0.f;               // rad/s
    float bend = 0.f;               // radians between the hind and the fore legs
    float drop = 0.f;               // metres (model space) the body came down for its legs to reach their feet
    glm::vec3 last_position = glm::vec3(0.f);
    float last_yaw = 0.f;
    bool started = false;
};

// Context variable : when `on`, every animal walks with this style (the debug menu)
struct GaitStyleOverride {
    bool on = false;
    GaitStyle style;
};

// Context variable : the gait models of the species, analysed on first use
struct GaitModels {
    const GaitModel& get(const AnimalSpecies& species, int clip);
    std::unordered_map<const AnimalSpecies*, std::unordered_map<int, GaitModel>> models;
};

// The pose of one animal that walks : `pose` holds the clip's pose (evaluate()); its world and skin matrices are
// bent and planted in place (its local joints stay the clip's).
// transform : where the entity is, dt : seconds since the last call. Pure but for `gait`.
void proceduralWalk(const AnimalSpecies& species, const GaitModel& model, const Transform& transform, float clip_time, float dt,
    const GroundField* ground, AnimalGait& gait, AnimalPose& pose);

// After AnimalAnimation : animals with an AnimalGait that play a gait clip get the procedural pose. It also sets
// the playback rate of the clip, so the steps match the speed over the ground.
class AnimalGaitSys : public System {
public:
    AnimalGaitSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalGait"; }
    void preinit() override;
    void init() override;
    void tick(double dt) override;

private:
    std::shared_ptr<GroundField> ground;
};

}

#endif
