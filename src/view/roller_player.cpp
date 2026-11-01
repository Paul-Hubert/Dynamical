#include "roller_player.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>

#include "logic/agents/sphere_roller.h"
#include "logic/components/camera.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/components/vr.h"
#include "physics/physics_sys.h"
#include "scene/world.h"
#include "view/components/input.h"
#include "view/world_stepper.h"
#include "util/log.h"

using namespace dy;

RollerPlayerSys::RollerPlayerSys(entt::registry& reg, const nlohmann::json& params, Player player) : System(reg), player(player) {
    world = params.value("world", world);
    deadzone = params.value("deadzone", deadzone);
    stick_speed = params.value("stick_speed", stick_speed);
    lean_speed = params.value("lean_speed", lean_speed);
    lean_range = params.value("lean_range", lean_range);
    lean_deadzone = params.value("lean_deadzone", lean_deadzone);
    if(auto it = params.find("head_top"); it != params.end() && it->is_array() && it->size() == 3) {
        head_top = glm::vec3(it->at(0).get<float>(), it->at(1).get<float>(), it->at(2).get<float>());
    }
    floor_smoothing = params.value("floor_smoothing", floor_smoothing);
    eye_height = params.value("eye_height", eye_height);
    if(!(lean_deadzone > 0.f && lean_range > lean_deadzone)) {
        dy::log(dy::error) << name() << ": needs 0 < lean_deadzone < lean_range, got " << lean_deadzone << " and " << lean_range
            << "; using 0.01 and 0.08\n";
        lean_deadzone = 0.01f;
        lean_range = 0.08f;
    }
}

void RollerPlayerSys::init() {
    if(player != Player::VR) return;
    if(auto* rig = reg.ctx().find<VRRig>()) rig->stick_locomotion = false;
}

glm::vec3 RollerPlayerSys::vrCommand() {

    auto* vr = reg.ctx().find<VRInput>();
    auto* rig = reg.ctx().find<VRRig>();
    // No input while the headset is off or in a system menu: the sphere stops
    if(!vr || !vr->session_running || !vr->focused || !vr->head.valid || !rig) return glm::vec3(0.f);

    // A (its press, not its hold) puts the lean's origin at the top of the head
    if(vr->hands[VRInput::right].primary_pressed) {
        lean_origin = vr->head.position + vr->head.rotation * head_top;
        leaning = true;
    }

    glm::vec2 stick = vr->hands[VRInput::left].thumbstick;
    float deflection = std::min(glm::length(stick), 1.f);
    if(deflection > deadzone) {
        glm::vec3 forward = glm::mat3(rig->transform()) * (vr->head.rotation * glm::vec3(0.f, 0.f, -1.f));
        forward.y = 0.f;
        if(glm::length(forward) < 0.01f) return glm::vec3(0.f);
        forward = glm::normalize(forward);
        glm::vec3 right = glm::cross(forward, glm::vec3(0.f, 1.f, 0.f));
        glm::vec3 direction = forward * stick.y + right * stick.x;
        if(glm::length(direction) < 1e-4f) return glm::vec3(0.f);
        return glm::normalize(direction) * (stick_speed * (deflection - deadzone) / (1.f - deadzone));
    }

    if(leaning) {
        // The stick at rest: the top of the head's horizontal offset from the origin, in stage space, turned to the
        // world with the play area
        glm::vec3 lean = vr->head.position + vr->head.rotation * head_top - lean_origin;
        lean.y = 0.f;
        float distance = glm::length(lean);
        float speed = leanSpeed(distance, lean_deadzone, lean_range, lean_speed);
        if(speed > 0.f) return glm::mat3(rig->transform()) * (lean / distance) * speed;
    }
    return glm::vec3(0.f);

}

glm::vec3 RollerPlayerSys::desktopCommand() {

    auto* input = reg.ctx().find<Input>();
    auto* camera = reg.ctx().find<Camera>();
    if(!input || !camera || ImGui::GetIO().WantCaptureKeyboard) return glm::vec3(0.f);

    // Relative to where the camera looks (PlayerControl turns it with the right mouse button)
    glm::vec3 forward(-std::sin(camera->yaw), 0.f, -std::cos(camera->yaw));
    glm::vec3 right = camera->right();
    glm::vec3 direction(0.f);
    if(input->on[Input::FORWARD])  direction += forward;
    if(input->on[Input::BACKWARD]) direction -= forward;
    if(input->on[Input::RIGHT])    direction += right;
    if(input->on[Input::LEFT])     direction -= right;
    if(glm::length(direction) < 1e-4f) return glm::vec3(0.f);
    return glm::normalize(direction) * stick_speed * (input->on[Input::SPRINT] ? 1.f : 2.f / 3.f);

}

void RollerPlayerSys::tick(double dt) {

    OPTICK_EVENT();

    auto* worlds = reg.ctx().find<Worlds>();
    if(!worlds || world < 0 || world >= (int) worlds->size()) return;
    World& w = (*worlds)[world];
    auto view = w.reg.view<SphereAgent, Transform, RigidBody>();
    if(view.begin() == view.end()) return;
    entt::entity e = *view.begin();

    steerSphere(w.reg.get<SphereAgent>(e), player == Player::VR ? vrCommand() : desktopCommand());

    // The player's floor on the sphere, where WorldView draws it: between the last two ticks
    const auto* stepper = reg.ctx().find<WorldStepperState>();
    const float alpha = stepper ? (float) stepper->alpha : 1.f;
    glm::vec3 position = w.reg.get<Transform>(e).position;
    if(auto* pose = w.reg.try_get<PhysicsPose>(e)) position = glm::mix(pose->prev_position, pose->position, alpha);
    float bottom = position.y - w.reg.get<RigidBody>(e).half_extent.x;
    // Snaps on the first frame and after a respawn
    if(!placed || std::abs(bottom - floor) > 2.f) floor = bottom;
    else floor += (bottom - floor) * (1.f - std::exp(-floor_smoothing * (float) dt));
    placed = true;

    if(player == Player::VR) {
        if(auto* rig = reg.ctx().find<VRRig>()) rig->position = glm::vec3(position.x, floor, position.z);
    } else if(auto* camera = reg.ctx().find<Camera>()) {
        camera->position = glm::vec3(position.x, floor + eye_height, position.z);
    }

}
