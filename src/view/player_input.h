#ifndef DY_VIEW_PLAYER_INPUT_H
#define DY_VIEW_PLAYER_INPUT_H

#include "logic/systems/system.h"

struct _SDL_GameController;

namespace dy {

// The viewer's side of the physical players (docs/players.md, logic/player/): the devices of the window

// The keyboard and the mouse to the MouseKeyboardInput of the DesktopView player: which keys are down (by where they
// are on the keyboard), how the mouse moved, its buttons and its wheel. What they mean is the component's to say.
// The mouse is captured; left alt frees it for the menus and takes it back
class MouseKeyboardInputSys : public System {
public:
    MouseKeyboardInputSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "MouseKeyboardInput"; }
    void tick(double dt) override;
    void finish() override;

private:
    bool captured = true;
    double time = 0.;
};

// The first gamepad SDL finds to the ControllersInput of the DesktopView player: sticks, triggers, A, X, the left
// shoulder, the d-pad. Looked for again every second while there is none
class ControllersInputSys : public System {
public:
    ControllersInputSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "ControllersInput"; }
    void preinit() override;
    void tick(double dt) override;
    void finish() override;

private:
    _SDL_GameController* pad = nullptr;
    bool started = false;
    bool was_up = false, was_down = false;
    double time = 0.;
    double search = 0.;
};

// A box model on the hands and the props, which the simulation makes without one
class PlayerModelsSys : public System {
public:
    PlayerModelsSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "PlayerModels"; }
    void tick(double dt) override;
};

}

#endif
