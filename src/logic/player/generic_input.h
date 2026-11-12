#ifndef DY_GENERIC_PLAYER_INPUT_H
#define DY_GENERIC_PLAYER_INPUT_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <bitset>

namespace dy {

// What a player asks, whatever it asks it with (Unity's GenericPlayerInput, Assets/Input/Scripts). A player entity
// carries one or more input components; each holds the state of its device, written once a frame by the device's
// system, and answers the same questions about it. Whoever needs the player's input asks playerInput(reg, entity)
// and calls the functions: nothing copies the answers into a common struct.
//
//   MouseKeyboardInput   WASD by where the keys are, the mouse looks          <- MouseKeyboardInput (viewer)
//   ControllersInput     a gamepad: left stick walks, right stick looks       <- ControllersInput (viewer)
//   VRPlayerInput        a headset's controllers                              <- VRPlayerInput
//   ScriptedInput        plain values: tests, scripts
//
// Buttons are states, not presses: who needs the frame of a press keeps the last state it saw
class GenericPlayerInput {
public:
    virtual ~GenericPlayerInput() = default;

    virtual glm::vec2 getMovementAxis() const = 0;      // x right, y forward, length <= 1
    virtual glm::vec2 getViewAxis() const = 0;          // x right, y up: a stick's deflection, or radians turned this frame
    virtual bool viewIsDelta() const { return false; }  // ... the second: a mouse
    virtual bool getJumpButton() const = 0;
    virtual bool getPrimaryActionButton() const = 0;
    virtual bool getSecondaryActionButton() const = 0;
    virtual bool getInteractButton() const = 0;
    virtual bool getRunButton() const = 0;              // Unity's GetWalkingButton
    virtual float getControlAxis() const = 0;           // notches this frame (a mouse wheel)
    virtual bool isActive() const = 0;                  // the device is there and its player at it

    double last_action = -1.;   // seconds of play at which the device was last used: the latest one speaks for the player
};

// Unity's KeyboardPlayerInput. Keys are physical ones: W, A, S, D are where they are on a QWERTY keyboard, whatever
// the layout says is printed on them (Z, Q, S, D on an AZERTY one)
struct MouseKeyboardInput : GenericPlayerInput {
    enum Key { W, A, S, D, Space, LeftShift, E, key_count };

    std::bitset<key_count> keys;
    glm::vec2 mouse = glm::vec2(0.f);   // counts moved this frame, x right, y down
    bool left = false, right = false;   // buttons
    float wheel = 0.f;                  // notches this frame
    bool captured = true;               // the mouse is the player's (not the menus')
    float sensitivity = 0.0025f;        // radians per count

    glm::vec2 getMovementAxis() const override;
    glm::vec2 getViewAxis() const override { return captured ? glm::vec2(mouse.x, -mouse.y) * sensitivity : glm::vec2(0.f); }
    bool viewIsDelta() const override { return true; }
    bool getJumpButton() const override { return keys[Space]; }
    bool getPrimaryActionButton() const override { return captured && left; }
    bool getSecondaryActionButton() const override { return captured && right; }
    bool getInteractButton() const override { return keys[E]; }
    bool getRunButton() const override { return keys[LeftShift]; }
    float getControlAxis() const override { return captured ? wheel : 0.f; }
    bool isActive() const override { return true; }
};

// Unity's MultiControllerInput: a gamepad. The left stick walks, the right one looks, A jumps, the right trigger is
// the primary action, the left one the secondary, X interacts, the left shoulder runs. The control axis is the
// d-pad (a notch per press; Unity's was the right stick, which looks too)
struct ControllersInput : GenericPlayerInput {
    bool connected = false;
    glm::vec2 left_stick = glm::vec2(0.f);      // x right, y forward
    glm::vec2 right_stick = glm::vec2(0.f);     // x right, y up
    float left_trigger = 0.f, right_trigger = 0.f;
    bool a = false, x = false, left_shoulder = false;
    float dpad = 0.f;                   // +1 the frame up is pressed, -1 down
    float deadzone = 0.2f;

    glm::vec2 getMovementAxis() const override { return stick(left_stick); }
    glm::vec2 getViewAxis() const override { return stick(right_stick); }
    bool getJumpButton() const override { return connected && a; }
    bool getPrimaryActionButton() const override { return connected && right_trigger > 0.5f; }
    bool getSecondaryActionButton() const override { return connected && left_trigger > 0.5f; }
    bool getInteractButton() const override { return connected && x; }
    bool getRunButton() const override { return connected && left_shoulder; }
    float getControlAxis() const override { return connected ? dpad : 0.f; }
    bool isActive() const override { return connected; }

    // Nothing inside the dead zone, then from 0 to 1 at the rim
    glm::vec2 stick(glm::vec2 raw) const;
};

// Unity's VRPlayerInput: the left stick walks and the right one turns, both past half their travel, the left stick's
// click jumps, A (right hand) runs, the triggers are the actions (left: primary)
struct VRPlayerInput : GenericPlayerInput {
    bool focused = false;               // a running, focused session
    glm::vec2 left_stick = glm::vec2(0.f), right_stick = glm::vec2(0.f);
    bool left_stick_click = false;
    bool right_primary = false;
    float left_trigger = 0.f, right_trigger = 0.f;

    glm::vec2 getMovementAxis() const override;
    glm::vec2 getViewAxis() const override;
    bool getJumpButton() const override { return focused && left_stick_click; }
    bool getPrimaryActionButton() const override { return focused && left_trigger > 0.5f; }
    bool getSecondaryActionButton() const override { return focused && right_trigger > 0.5f; }
    bool getInteractButton() const override { return false; }
    bool getRunButton() const override { return focused && right_primary; }
    float getControlAxis() const override { return focused ? right_stick.y : 0.f; }
    bool isActive() const override { return focused; }
};

// Plain values, written by a test or a script. A player that has one listens to nothing else
struct ScriptedInput : GenericPlayerInput {
    glm::vec2 move = glm::vec2(0.f);
    glm::vec2 view = glm::vec2(0.f);
    bool view_is_delta = false;
    bool jump = false, run = false, primary = false, secondary = false, interact = false;
    float control = 0.f;

    glm::vec2 getMovementAxis() const override { return move; }
    glm::vec2 getViewAxis() const override { return view; }
    bool viewIsDelta() const override { return view_is_delta; }
    bool getJumpButton() const override { return jump; }
    bool getPrimaryActionButton() const override { return primary; }
    bool getSecondaryActionButton() const override { return secondary; }
    bool getInteractButton() const override { return interact; }
    bool getRunButton() const override { return run; }
    float getControlAxis() const override { return control; }
    bool isActive() const override { return true; }
};

// The input of a player (Unity's GenericPlayerInput.GetInput): its ScriptedInput when it has one, else, among its
// devices that are active, the one used last. An entity without any gets an input that asks for nothing
const GenericPlayerInput& playerInput(const entt::registry& reg, entt::entity entity);

}

#endif
