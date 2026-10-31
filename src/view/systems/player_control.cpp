#include "system_list.h"

#include <SDL.h>

#include <algorithm>
#include <memory>

#include "logic/components/camera.h"
#include "view/components/input.h"
#include "logic/components/vr.h"
#include "logic/terrain/terrain_generator.h"

#include <imgui/imgui.h>

using namespace dy;

void PlayerControlSys::preinit() {
    reg.ctx().emplace<Camera>();
}

void PlayerControlSys::init() {

    // Start a bit above the ground, looking at the demo objects around the origin
    auto& camera = reg.ctx().get<Camera>();
    camera.position = glm::vec3(0.f, 0.f, 60.f);
    camera.pitch = -0.3f;
    if(auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>()) {
        camera.position.y = (*generator)->surfaceHeight(camera.position.x, camera.position.z) + 20.f;
    }

}

void PlayerControlSys::tick(double dt) {

    OPTICK_EVENT();

    auto& camera = reg.ctx().get<Camera>();
    auto& input = reg.ctx().get<Input>();

    // Look around while the right mouse button is held
    bool looking = input.rightDown;
    SDL_SetRelativeMouseMode(looking ? SDL_TRUE : SDL_FALSE);
    if(looking) {
        constexpr float sensitivity = 0.0025f;
        camera.yaw -= input.mouseDelta.x * sensitivity;
        camera.pitch = std::clamp(camera.pitch - input.mouseDelta.y * sensitivity, -1.55f, 1.55f);
    }

    if(ImGui::GetIO().WantCaptureKeyboard) return;

    static float base_speed = 30.f;
    if(input.mouseWheel.y != 0 && !ImGui::GetIO().WantCaptureMouse) {
        base_speed = std::clamp(base_speed * (input.mouseWheel.y > 0 ? 1.25f : 0.8f), 1.f, 2000.f);
    }

    float speed = base_speed * (input.on[Input::SPRINT] ? 8.f : 1.f) * (float) dt;

    glm::vec3 move(0.f);
    if(input.on[Input::FORWARD])  move += camera.forward();
    if(input.on[Input::BACKWARD]) move -= camera.forward();
    if(input.on[Input::RIGHT])    move += camera.right();
    if(input.on[Input::LEFT])     move -= camera.right();
    if(input.on[Input::UP])       move += glm::vec3(0.f, 1.f, 0.f);
    if(input.on[Input::DOWN])     move -= glm::vec3(0.f, 1.f, 0.f);

    camera.sprinting = input.on[Input::SPRINT];
    if(glm::length(move) > 0.f) {
        camera.position += glm::normalize(move) * speed;
    }

    // Bring the VR play area under the desktop camera
    if(input.on[Input::VR_RIG]) {
        input.on.set(Input::VR_RIG, false);
        auto& rig = reg.ctx().get<VRRig>();
        rig.position = camera.position;
        if(auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>()) {
            rig.position.y = (*generator)->surfaceHeight(rig.position.x, rig.position.z);
        }
        rig.yaw = camera.yaw;
    }

}

void PlayerControlSys::finish() {

}
