#ifndef DY_VR_ROLLER_H
#define DY_VR_ROLLER_H

#include "logic/systems/system.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace dy {

// Host system of vr_roller (docs/physical-agents/sphere-roller.md): the trained sphere roller is the player's
// locomotion, in VR and on the desktop. It commands the velocity of one world's steered sphere (steerSphere):
// - the left stick, relative to where the head looks: full deflection is stick_speed;
// - with the stick at rest, the lean, once A has been pressed: the horizontal offset of the top of the head from
//   where it was at the last press of A gives the direction, and leanSpeed its speed (0 within lean_deadzone,
//   logarithmic, lean_speed from lean_range on). The top of the head (head_top, in head space: up and back from
//   the eyes) sits over the neck's yaw axis, so looking around does not move it;
// - without a running VR session, the desktop player's WASD, relative to where the desktop camera looks (2/3 of
//   stick_speed, all of it with shift).
// The player stands on the sphere: the play area's floor and the desktop player's feet are at its lowest point,
// smoothed in height so bumps don't shake the view. The desktop camera is the desktop player's eyes, eye_height
// above that floor, turned by PlayerControl (right mouse button); while a VR session runs it shows the headset's
// view. The play area's yaw stays the player's (right stick). Takes the left stick from VRControl and the
// desktop camera from PlayerControl. After PlayerControl and WorldStepper (the poses of this frame's ticks), and
// before VRControl (which places the hands in the moved play area).
// params: { "world": 0, "deadzone": 0.15, "stick_speed": 3.0, "lean_speed": 3.0, "lean_range": 0.08,
//           "lean_deadzone": 0.01, "head_top": [0, 0.11, 0.09], "eye_height": 1.6, "floor_smoothing": 10.0 }
class VRRollerSys : public System {
public:
    VRRollerSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "VRRoller"; }
    void init() override;
    void tick(double dt) override;

private:
    int world = 0;
    float deadzone = 0.15f;
    float stick_speed = 3.f;        // m/s, training's top target speed
    float lean_speed = 3.f;         // m/s
    float lean_range = 0.08f;       // m
    float lean_deadzone = 0.01f;    // m
    glm::vec3 head_top = glm::vec3(0.f, 0.11f, 0.09f);  // m, OpenXR view space (y up, -z forward)
    float eye_height = 1.6f;        // m, the desktop player's eyes above the floor
    float floor_smoothing = 10.f;   // 1/s

    bool leaning = false;           // A has been pressed
    glm::vec3 lean_origin = glm::vec3(0.f); // stage space
    bool placed = false;
    float floor = 0.f;
};

}

#endif
