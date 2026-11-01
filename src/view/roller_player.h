#ifndef DY_ROLLER_PLAYER_H
#define DY_ROLLER_PLAYER_H

#include "logic/systems/system.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace dy {

// Host system of vr_roller (docs/physical-agents/sphere-roller.md): a player whose locomotion is one world's
// trained, steered sphere (steerSphere). The player stands on it: their floor is the sphere's lowest point, smoothed
// in height so bumps don't shake the view. Two registry names, one per player, each on its own world:
//
// VRRoller, the VR player: it owns the play area (VRRig) and takes the left stick from VRControl. The command is
// - the left stick, relative to where the head looks: full deflection is stick_speed;
// - with the stick at rest, the lean, once A has been pressed: the horizontal offset of the top of the head from
//   where it was at the last press of A gives the direction, and leanSpeed its speed (0 within lean_deadzone,
//   logarithmic, lean_speed from lean_range on). The top of the head (head_top, in head space: up and back from
//   the eyes) sits over the neck's yaw axis, so looking around does not move it;
// - zero without a focused VR session (headset off, system menu, no headset).
// The play area's yaw stays the player's (right stick). Before VRControl, which places the hands in the moved
// play area.
//
// DesktopRoller, the desktop player: it owns the desktop camera, their eyes eye_height above the floor, turned by
// PlayerControl (right mouse button). The command is WASD relative to where the camera looks (2/3 of stick_speed,
// all of it with shift). After PlayerControl.
//
// Both after WorldStepper (the poses of this frame's ticks).
// params: { "world": 0, "deadzone": 0.15, "stick_speed": 3.0, "lean_speed": 3.0, "lean_range": 0.08,
//           "lean_deadzone": 0.01, "head_top": [0, 0.11, 0.09], "eye_height": 1.6, "floor_smoothing": 10.0 }
class RollerPlayerSys : public System {
public:
    enum class Player { VR, Desktop };

    RollerPlayerSys(entt::registry& reg, const nlohmann::json& params, Player player);
    const char* name() override { return player == Player::VR ? "VRRoller" : "DesktopRoller"; }
    void init() override;
    void tick(double dt) override;

private:
    glm::vec3 vrCommand();
    glm::vec3 desktopCommand();

    Player player;
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
