#include "player_input.h"

#include <SDL.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>

#include "logic/components/transform.h"
#include "logic/player/player.h"
#include "renderer/model/model_manager.h"
#include "scene/run_args.h"
#include "view/components/input.h"
#include "view/components/model_renderable.h"

using namespace dy;

void MouseKeyboardInputSys::tick(double dt) {

    auto& in = reg.ctx().get<Input>();
    time += dt;

    if(in.on[Input::CURSOR]) {
        in.on.set(Input::CURSOR, false);
        captured = !captured;
    }
    SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
    // The menus do not get the clicks of a captured mouse
    if(captured) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

    const bool keys = !ImGui::GetIO().WantCaptureKeyboard;

    for(auto [entity, input] : reg.view<DesktopView, MouseKeyboardInput>().each()) {

        // InputSys maps the scancodes: the places of W, A, S, D, space, left shift and E on the keyboard
        using Key = MouseKeyboardInput::Key;
        input.keys.reset();
        if(keys) {
            input.keys.set(Key::W, in.on[Input::FORWARD]);
            input.keys.set(Key::S, in.on[Input::BACKWARD]);
            input.keys.set(Key::D, in.on[Input::RIGHT]);
            input.keys.set(Key::A, in.on[Input::LEFT]);
            input.keys.set(Key::Space, in.on[Input::UP]);
            input.keys.set(Key::LeftShift, in.on[Input::SPRINT]);
            input.keys.set(Key::E, in.on[Input::INTERACT]);
        }
        input.captured = captured;
        input.mouse = glm::vec2(in.mouseDelta);
        // The raw buttons: the UI takes leftDown while the cursor was last over a window
        input.left = in.mouseLeft;
        input.right = in.mouseRight;
        input.wheel = (float) in.mouseWheel.y;

        if(input.keys.any() || (captured && (input.mouse != glm::vec2(0.f) || input.left || input.right || input.wheel != 0.f))) input.last_action = time;

    }

}

void MouseKeyboardInputSys::finish() {
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    SDL_SetRelativeMouseMode(SDL_FALSE);
}



void ControllersInputSys::preinit() {
    // Captures and probes are the same with or without a gamepad on the desk
    auto* args = reg.ctx().find<RunArgs>();
    if(args && args->deterministic()) return;
    started = SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0;
}

void ControllersInputSys::tick(double dt) {

    time += dt;
    if(started) {
        if(pad && !SDL_GameControllerGetAttached(pad)) {
            SDL_GameControllerClose(pad);
            pad = nullptr;
        }
        search -= dt;
        if(!pad && search <= 0.) {
            search = 1.;
            for(int i = 0; i < SDL_NumJoysticks() && !pad; i++) {
                if(SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
            }
        }
    }

    auto axis = [&](SDL_GameControllerAxis a) { return std::clamp((float) SDL_GameControllerGetAxis(pad, a) / 32767.f, -1.f, 1.f); };
    auto button = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad, b) != 0; };

    for(auto [entity, input] : reg.view<DesktopView, ControllersInput>().each()) {

        input.connected = pad != nullptr;
        if(!pad) continue;

        // SDL's sticks are positive down
        input.left_stick = glm::vec2(axis(SDL_CONTROLLER_AXIS_LEFTX), -axis(SDL_CONTROLLER_AXIS_LEFTY));
        input.right_stick = glm::vec2(axis(SDL_CONTROLLER_AXIS_RIGHTX), -axis(SDL_CONTROLLER_AXIS_RIGHTY));
        input.left_trigger = axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        input.right_trigger = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        input.a = button(SDL_CONTROLLER_BUTTON_A);
        input.x = button(SDL_CONTROLLER_BUTTON_X);
        input.left_shoulder = button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        const bool up = button(SDL_CONTROLLER_BUTTON_DPAD_UP), down = button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        input.dpad = (up && !was_up ? 1.f : 0.f) - (down && !was_down ? 1.f : 0.f);
        was_up = up;
        was_down = down;

        if(input.getMovementAxis() != glm::vec2(0.f) || input.getViewAxis() != glm::vec2(0.f) || input.a || input.x || input.left_shoulder
            || input.left_trigger > 0.5f || input.right_trigger > 0.5f || up || down) input.last_action = time;

    }

}

void ControllersInputSys::finish() {
    if(pad) SDL_GameControllerClose(pad);
    pad = nullptr;
    if(started) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    started = false;
}

void PlayerModelsSys::tick(double dt) {

    auto* models = reg.ctx().find<ModelManager>();
    if(!models) return;

    std::vector<std::pair<entt::entity, glm::vec4>> bare;
    for(auto [entity, hand] : reg.view<Hand>(entt::exclude<ModelRenderable>).each()) {
        bare.push_back({ entity, hand.side == VRInput::left ? glm::vec4(0.2f, 0.4f, 1.f, 1.f) : glm::vec4(1.f, 0.3f, 0.2f, 1.f) });
    }
    for(auto [entity, prop] : reg.view<Prop>(entt::exclude<ModelRenderable>).each()) {
        bare.push_back({ entity, prop.color });
    }
    if(bare.empty()) return;

    // A unit cube: the Transform's scale is the box
    entt::entity box = models->load("./resources/models/box.gltf");
    if(box == entt::null) return;
    for(auto& [entity, color] : bare) reg.emplace<ModelRenderable>(entity, box, color);

}
