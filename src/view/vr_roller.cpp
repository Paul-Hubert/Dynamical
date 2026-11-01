#include "vr_roller.h"

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

VRRollerSys::VRRollerSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
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
        dy::log(dy::error) << "VRRoller: needs 0 < lean_deadzone < lean_range, got " << lean_deadzone << " and " << lean_range
            << "; using 0.01 and 0.08\n";
        lean_deadzone = 0.01f;
        lean_range = 0.08f;
    }
}

void VRRollerSys::init() {
    if(auto* rig = reg.ctx().find<VRRig>()) rig->stick_locomotion = false;
}

void VRRollerSys::tick(double dt) {

    OPTICK_EVENT();

    auto* worlds = reg.ctx().find<Worlds>();
    if(!worlds || world < 0 || world >= (int) worlds->size()) return;
    World& w = (*worlds)[world];
    auto view = w.reg.view<SphereAgent, Transform, RigidBody>();
    if(view.begin() == view.end()) return;
    entt::entity e = *view.begin();
    auto& agent = w.reg.get<SphereAgent>(e);
    auto* rig = reg.ctx().find<VRRig>();
    auto* camera = reg.ctx().find<Camera>();

    // The command, a horizontal velocity
    glm::vec3 command(0.f);
    auto* vr = reg.ctx().find<VRInput>();
    if(vr && vr->session_running) {
        // A (its press, not its hold) puts the lean's origin at the top of the head
        if(vr->focused && vr->hands[VRInput::right].primary_pressed && vr->head.valid) {
            lean_origin = vr->head.position + vr->head.rotation * head_top;
            leaning = true;
        }
        glm::vec2 stick = vr->hands[VRInput::left].thumbstick;
        float deflection = std::min(glm::length(stick), 1.f);
        // No input while the headset is off or in a system menu: the sphere stops
        if(vr->focused && vr->head.valid && rig && deflection > deadzone) {
            glm::vec3 forward = glm::mat3(rig->transform()) * (vr->head.rotation * glm::vec3(0.f, 0.f, -1.f));
            forward.y = 0.f;
            if(glm::length(forward) > 0.01f) {
                forward = glm::normalize(forward);
                glm::vec3 right = glm::cross(forward, glm::vec3(0.f, 1.f, 0.f));
                glm::vec3 direction = forward * stick.y + right * stick.x;
                if(glm::length(direction) > 1e-4f) command = glm::normalize(direction) * (stick_speed * (deflection - deadzone) / (1.f - deadzone));
            }
        } else if(vr->focused && vr->head.valid && rig && leaning) {
            // The stick at rest: the top of the head's horizontal offset from the origin, in stage space, turned to
            // the world with the play area
            glm::vec3 lean = vr->head.position + vr->head.rotation * head_top - lean_origin;
            lean.y = 0.f;
            float distance = glm::length(lean);
            float speed = leanSpeed(distance, lean_deadzone, lean_range, lean_speed);
            if(speed > 0.f) command = glm::mat3(rig->transform()) * (lean / distance) * speed;
        }
    } else if(auto* input = reg.ctx().find<Input>(); input && camera && !ImGui::GetIO().WantCaptureKeyboard) {
        // The desktop player: relative to where the camera looks (PlayerControl turns it with the right mouse button)
        glm::vec3 forward(-std::sin(camera->yaw), 0.f, -std::cos(camera->yaw));
        glm::vec3 right = camera->right();
        glm::vec3 direction(0.f);
        if(input->on[Input::FORWARD])  direction += forward;
        if(input->on[Input::BACKWARD]) direction -= forward;
        if(input->on[Input::RIGHT])    direction += right;
        if(input->on[Input::LEFT])     direction -= right;
        if(glm::length(direction) > 1e-4f) command = glm::normalize(direction) * stick_speed * (input->on[Input::SPRINT] ? 1.f : 2.f / 3.f);
    }
    steerSphere(agent, command);

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
    if(rig) rig->position = glm::vec3(position.x, floor, position.z);

    // The desktop camera: the headset's view while a VR session runs, else the desktop player's eyes, eye_height
    // above the floor (its yaw and pitch stay PlayerControl's)
    if(!camera) return;
    if(vr && vr->session_running && vr->head.valid && rig) {
        glm::mat4 world_from_stage = rig->transform();
        camera->position = glm::vec3(world_from_stage * glm::vec4(vr->head.position, 1.f));
        glm::vec3 look = glm::mat3(world_from_stage) * (vr->head.rotation * glm::vec3(0.f, 0.f, -1.f));
        camera->pitch = std::asin(std::clamp(look.y, -1.f, 1.f));
        camera->yaw = std::atan2(-look.x, -look.z);
    } else {
        camera->position = glm::vec3(position.x, floor + eye_height, position.z);
    }

}
