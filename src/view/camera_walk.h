#ifndef DY_CAMERA_WALK_H
#define DY_CAMERA_WALK_H

#include "logic/systems/system.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dy {

class GroundField;
class SoilField;
class VegetationLibrary;

// Context variable : where the walk of CameraWalkSys is
struct CameraWalk {
    glm::vec2 start = glm::vec2(0.f);   // x, z
    float heading = 0.f;                // radians, as Camera::yaw
    float distance = 0.f;
    float progress = 0.f;               // metres along the line
    bool started = false;               // false while the terrain and the plants load
    bool finished = false;
};

// Walks the desktop camera over the terrain at eye height along a line, by the path that best keeps clear of the
// trunks and of the branches at eye height : videos of the forests (tools/forest_video.py). Waits for the terrain
// and the plants to be loaded, stops at the end of the line. After PlayerControl in the scene, which it overrides.
//
// params : "distance" (280 m), "speed" (4 m/s), "eye_height" (1.7 m),
//          "sway" (8 degrees : the camera looks from side to side),
//          "start" ([x, z]) and "heading" (degrees, 0 toward -z, 90 toward -x) : the line. Without them, with a
//          Forest in the scene, a line is picked within "search" (1200 m) of the origin : from open land through
//          a forest and out of it, over more than one kind of forest when it can,
//          "lead" (metres of open land at each end of the line; 0 : 45 m, less on a short line),
//          "soil" (0 : how much the line is picked for the mud, the stones and the sand its open ends cross
//          (docs/ground.md), against 1 for the forest; over 0 the camera looks down at them as it gets there),
//          "herds" (0, metres : the line is picked for the herds whose home is this close to it, and the camera
//          turns to the nearest one within that distance; Herds before this system in the scene)
//          "exit" (false : the process ends with the walk : a profile of the walk, tools/profile_run.py)
class CameraWalkSys : public System {
public:
    CameraWalkSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "CameraWalk"; }
    void preinit() override;
    void init() override;
    void tick(double dt) override;

private:
    // How far a species reaches from its foot, by height (bins of `bin` metres) and by direction (model space)
    struct Reach {
        static constexpr int bins = 20, sectors = 24;
        static constexpr float bin = 0.25f;
        static int sector(float x, float z) {
            int k = (int) std::floor((std::atan2(z, x) + 3.14159265f) / (2.f * 3.14159265f) * sectors);
            return k < 0 ? 0 : k >= sectors ? sectors - 1 : k;
        }
        bool known = false;
        std::array<std::array<float, sectors>, bins> radius {};
    };
    const Reach& reach(const VegetationLibrary& library, uint32_t species);

    void plan();
    void route();
    float groundHeight(float x, float z, float near_y) const;
    void place();
    // Of a line, for its score : the bare soil its open ends cross (0 to 1), the herds along it (0 to 1)
    float soilOn(glm::vec2 from, glm::vec2 dir, float lead) const;
    float herdsBy(glm::vec2 from, glm::vec2 dir, float lead);
    // What the camera turns to : the soil ahead, the nearest herd
    void look(float h);

    float distance = 280.f;
    float speed = 4.f;
    float eye_height = 1.7f;
    float sway = 8.f;
    float search = 1200.f;
    float open_lead = 0.f;
    float soil = 0.f;
    float herds = 0.f;
    bool has_line = false;
    bool exit_at_end = false;
    glm::vec2 start = glm::vec2(0.f);
    float heading = 0.f;

    std::shared_ptr<GroundField> ground;
    std::shared_ptr<SoilField> soil_field;      // with "soil"
    std::unordered_map<uint64_t, glm::vec3> herd_homes;     // by cell : x, z, and 1 with a herd

    std::unordered_map<uint32_t, Reach> reaches;    // by species
    std::vector<glm::vec2> path;    // around the plants, from the start of the line to its end
    size_t path_at = 0;

    glm::vec2 position = glm::vec2(0.f);
    float height = 0.f;         // of the eye
    float climb = 0.f;          // slope of the path, smoothed
    float direction = 0.f;      // radians, as Camera::yaw
    float turn_rate = 0.f;
    // How much the camera looks at something (0 : along the path), where from its direction and how far up, with
    // their rates : critically damped
    float looking = 0.f, look_yaw = 0.f, look_pitch = 0.f;
    float looking_rate = 0.f, look_yaw_rate = 0.f, look_pitch_rate = 0.f;
    glm::ivec2 watching = glm::ivec2(1 << 30);      // cell of the herd it last turned to
    double time = 0.;
    int frames = 0;
};

}

#endif
