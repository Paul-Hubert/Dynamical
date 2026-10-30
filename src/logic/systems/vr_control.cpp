#include "system_list.h"

#include <memory>

#include "logic/scene_spawn.h"
#include "logic/components/model_renderable.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/components/vr.h"
#include "logic/terrain/terrain_generator.h"

#include "renderer/model/model_manager.h"

using namespace dy;

namespace {

// Entities following the controllers
struct VRHands {
    entt::entity hands[2] = { entt::null, entt::null };
};

constexpr float hand_size = 0.08f;
constexpr float move_speed = 4.f;     // m/s at full thumbstick
constexpr float turn_speed = 1.5f;    // rad/s at full thumbstick

}

void VRControlSys::preinit() {
    reg.ctx().emplace<VRHands>();
}

void VRControlSys::init() {

    // The play area starts on the ground at the origin
    auto& rig = reg.ctx().get<VRRig>();
    if(auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>()) {
        rig.position.y = (*generator)->surfaceHeight(0.f, 0.f);
    }

    auto* input = reg.ctx().find<VRInput>();
    if(!input || !input->enabled) return;

    entt::entity box = reg.ctx().get<ModelManager>().load("./resources/models/box.gltf");
    if(box == entt::null) return;

    auto& hands = reg.ctx().get<VRHands>();
    const glm::vec4 colors[2] = { glm::vec4(0.2f, 0.4f, 1.f, 1.f), glm::vec4(1.f, 0.3f, 0.2f, 1.f) };
    for(int i = 0; i < 2; i++) {
        hands.hands[i] = reg.create();
        reg.emplace<Transform>(hands.hands[i], glm::vec3(0.f), glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(0.f));
        reg.emplace<ModelRenderable>(hands.hands[i], box, colors[i]);
        // Kinematic : the hands push dynamic bodies around
        auto& body = reg.emplace<RigidBody>(hands.hands[i]);
        body.motion = RigidBody::Motion::Kinematic;
        body.half_extent = glm::vec3(hand_size * 0.5f);
    }

}

void VRControlSys::tick(double dt) {

    OPTICK_EVENT();

    auto* input = reg.ctx().find<VRInput>();
    if(!input || !input->session_running) return;

    auto& rig = reg.ctx().get<VRRig>();
    auto& hands = reg.ctx().get<VRHands>();

    if(input->focused) {

        // Left stick : move on the horizontal plane, relative to where the head looks
        glm::vec2 stick = input->hands[VRInput::left].thumbstick;
        if(glm::length(stick) > 0.15f && input->head.valid) {
            glm::mat3 world_from_stage = glm::mat3(rig.transform());
            glm::vec3 forward = world_from_stage * (input->head.rotation * glm::vec3(0.f, 0.f, -1.f));
            forward.y = 0.f;
            if(glm::length(forward) > 0.01f) {
                forward = glm::normalize(forward);
                glm::vec3 right = glm::cross(forward, glm::vec3(0.f, 1.f, 0.f));
                rig.position += (forward * stick.y + right * stick.x) * move_speed * (float) dt;
            }
            if(auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>()) {
                rig.position.y = (*generator)->surfaceHeight(rig.position.x, rig.position.z);
            }
        }

        // Right stick : smooth turn
        float turn = input->hands[VRInput::right].thumbstick.x;
        if(std::abs(turn) > 0.3f) {
            rig.yaw -= turn * turn_speed * (float) dt;
        }

        // Right trigger : spawn a box in the hand, thrown with the hand velocity
        auto& right = input->hands[VRInput::right];
        if(right.trigger_pressed && right.pose.valid) {
            glm::mat4 world_from_stage = rig.transform();
            glm::vec3 position = glm::vec3(world_from_stage * glm::vec4(right.pose.position + right.pose.rotation * glm::vec3(0.f, 0.f, -0.2f), 1.f));
            glm::vec3 velocity = glm::mat3(world_from_stage) * right.linear_velocity;
            spawnBox(reg, position, 0.3f, glm::vec4(1.f, 0.8f, 0.3f, 1.f), velocity * 1.5f);
        }

    }

    // Controller models, hidden when not tracked
    glm::mat4 world_from_stage = rig.transform();
    glm::quat rig_rotation = glm::angleAxis(rig.yaw, glm::vec3(0.f, 1.f, 0.f));
    for(int i = 0; i < 2; i++) {
        if(hands.hands[i] == entt::null) continue;
        auto& transform = reg.get<Transform>(hands.hands[i]);
        auto& pose = input->hands[i].pose;
        transform.position = glm::vec3(world_from_stage * glm::vec4(pose.position, 1.f));
        transform.rotation = rig_rotation * pose.rotation;
        transform.scale = glm::vec3(pose.valid ? hand_size : 0.f);
    }

}

void VRControlSys::finish() {

}
