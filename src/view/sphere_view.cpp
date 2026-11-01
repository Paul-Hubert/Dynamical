#include "sphere_view.h"

#include <glm/gtc/quaternion.hpp>
#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>

#include "logic/agents/agent_io.h"
#include "logic/agents/sphere_roller.h"
#include "logic/components/camera.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "physics/physics_sys.h"
#include "physics/physics_world.h"
#include "renderer/model/model_manager.h"
#include "scene/world.h"
#include "view/components/model_renderable.h"
#include "view/world_stepper.h"
#ifdef DYNAMICAL_ONNX
#include "onnx/policy_systems.h"
#endif

using namespace dy;

namespace {

const char* const box_model = "./resources/models/primitives/box.gltf";

enum Part { TargetBar = 0, VelocityBar = 1, Dot0 = 2 };

// Rotation taking local Z to `dir` (the box is symmetric, the sign of Z does not matter)
glm::quat along(glm::vec3 dir) {
    float length = glm::length(dir);
    if(length < 1e-6f) return glm::quat(1.f, 0.f, 0.f, 0.f);
    dir /= length;
    glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(1.f, 0.f, 0.f) : glm::vec3(0.f, 1.f, 0.f);
    return glm::quatLookAt(dir, up);
}

}

SphereViewSys::SphereViewSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    world = params.value("world", world);
    distance = params.value("distance", distance);
    height = params.value("height", height);
    smoothing = params.value("smoothing", smoothing);
    side = params.value("side", side);
    arrow_scale = params.value("arrow_scale", arrow_scale);
    camera = params.value("camera", camera);
    bars = params.value("bars", bars);
    follow = camera;
}

entt::entity SphereViewSys::part(int i) {
    if(parts[i] == entt::null || !reg.valid(parts[i])) {
        auto* models = reg.ctx().find<ModelManager>();
        if(!models) return entt::null;
        parts[i] = reg.create();
        reg.emplace<Transform>(parts[i]);
        reg.emplace<ModelRenderable>(parts[i], models->load(box_model));
    }
    return parts[i];
}

void SphereViewSys::place(int i, glm::vec3 position, glm::quat rotation, glm::vec3 scale, glm::vec4 color) {
    entt::entity e = part(i);
    if(e == entt::null) return;
    auto& t = reg.get<Transform>(e);
    t.position = position;
    t.rotation = rotation;
    t.scale = scale;
    reg.get<ModelRenderable>(e).tint = color;
}

void SphereViewSys::tick(double dt) {

    auto* worlds = reg.ctx().find<Worlds>();
    if(!worlds || worlds->size() == 0) return;
    world = std::clamp(world, 0, (int) worlds->size() - 1);
    World& w = (*worlds)[world];

    auto view = w.reg.view<SphereAgent, Transform, RigidBody>();
    if(view.begin() == view.end()) return;
    entt::entity e = *view.begin();
    const auto& agent = w.reg.get<SphereAgent>(e);
    const auto& body = w.reg.get<RigidBody>(e);
    auto& physics = w.reg.ctx().get<PhysicsWorld>();

    // Where WorldView draws it: between the last two ticks
    const auto* stepper = reg.ctx().find<WorldStepperState>();
    const float alpha = stepper ? (float) stepper->alpha : 1.f;
    glm::vec3 position = w.reg.get<Transform>(e).position;
    glm::quat rotation = w.reg.get<Transform>(e).rotation;
    if(auto* pose = w.reg.try_get<PhysicsPose>(e)) {
        position = glm::mix(pose->prev_position, pose->position, alpha);
        rotation = glm::slerp(pose->prev_rotation, pose->rotation, alpha);
    }
    glm::vec3 velocity(0.f);
    if(body.created()) velocity = toGlm(physics.bodies().GetLinearVelocity(JPH::BodyID(body.body)));
    glm::vec3 horizontal(velocity.x, 0.f, velocity.z);
    glm::vec3 target = agent.targetVelocity();
    const float r = body.half_extent.x;

    // Follow camera: behind the motion (the target when slow), smoothed, pulled in front of terrain in the way
    if(follow) {
        glm::vec3 wanted = glm::length(horizontal) > 0.5f ? glm::normalize(horizontal) : headingDirection(agent.heading);
        if(!have_direction) {
            direction = wanted;
            have_direction = true;
        }
        float k = 1.f - std::exp(-smoothing * (float) dt);
        glm::vec3 mixed = glm::mix(direction, wanted, k);
        if(glm::length(mixed) > 1e-4f) direction = glm::normalize(mixed);

        glm::vec3 look = position + glm::vec3(0.f, 0.3f, 0.f);
        glm::vec3 back = glm::angleAxis(side, glm::vec3(0.f, 1.f, 0.f)) * -direction;
        glm::vec3 eye = position + back * distance + glm::vec3(0.f, height, 0.f);
        if(auto hit = physics.castStatic(look, eye)) eye = hit->point + hit->normal * 0.3f;
        if(auto ground = physics.groundBelow(eye + glm::vec3(0.f, 3.f, 0.f), 3.5f)) eye.y = std::max(eye.y, ground->point.y + 0.5f);

        auto& camera = reg.ctx().get<Camera>();
        camera.position = eye;
        glm::vec3 d = glm::normalize(look - eye);
        camera.pitch = std::asin(std::clamp(d.y, -1.f, 1.f));
        camera.yaw = std::atan2(-d.x, -d.z);
    }

    // Bars from above the sphere: the target velocity and the horizontal velocity, arrow_scale metres per m/s
    glm::vec3 base = position + glm::vec3(0.f, r + 0.15f, 0.f);
    auto bar = [&](int i, glm::vec3 v, glm::vec4 color, float lift) {
        glm::vec3 length = v * arrow_scale;
        float l = glm::length(length);
        glm::vec3 start = base + glm::vec3(0.f, lift, 0.f);
        place(i, start + length * 0.5f, along(length), bars && l > 1e-3f ? glm::vec3(0.09f, 0.09f, l) : glm::vec3(0.f), color);
    };
    bar(TargetBar, target, glm::vec4(0.2f, 0.9f, 0.3f, 1.f), 0.f);
    bar(VelocityBar, horizontal, glm::vec4(1.f, 0.55f, 0.1f, 1.f), 0.08f);

    // Three dots on the surface turn with the sphere
    const glm::vec3 axes[3] = { glm::vec3(r, 0.f, 0.f), glm::vec3(0.f, r, 0.f), glm::vec3(0.f, 0.f, r) };
    for(int i = 0; i < 3; i++) {
        place(Dot0 + i, position + rotation * axes[i], rotation, glm::vec3(0.1f), glm::vec4(0.95f, 0.95f, 0.95f, 1.f));
    }

    // Panel; the engine's own panels start collapsed so the sphere stays in view
    const auto* io = w.reg.ctx().find<AgentIO>();
    ImGui::SetWindowCollapsed("Dynamical", true, ImGuiCond_FirstUseEver);
    ImGui::SetWindowCollapsed("Worlds", true, ImGuiCond_FirstUseEver);
    ImGui::SetWindowCollapsed("Frame time", true, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    if(ImGui::Begin("Sphere roller", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
#ifdef DYNAMICAL_ONNX
        if(const auto* source = reg.ctx().find<PolicySource>()) {
            if(!source->error.empty()) ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", source->error.c_str());
            else if(!source->file.empty()) ImGui::Text("policy %s (step %lld)", source->file.filename().string().c_str(), (long long) source->step);
        }
#endif
        ImGui::Text("world %d of %zu", world, worlds->size());
        ImGui::Text("target   %.2f m/s", agent.speed);
        ImGui::Text("speed    %.2f m/s", glm::length(horizontal));
        ImGui::Text("error    %.2f m/s", glm::length(horizontal - target));
        if(io) ImGui::Text("reward   %.3f", io->reward);
        if(agent.steered) ImGui::Text("decision %d (steered)", agent.decisions);
        else ImGui::Text("decision %d / %d", agent.decisions, agent.max_decisions);
        if(camera) ImGui::Checkbox("Follow", &follow);
        ImGui::Checkbox("Bars", &bars);
        if(worlds->size() > 1) ImGui::SliderInt("World", &world, 0, (int) worlds->size() - 1);
    }
    ImGui::End();

}

void SphereViewSys::finish() {
    for(auto& p : parts) {
        if(p != entt::null && reg.valid(p)) reg.destroy(p);
        p = entt::null;
    }
}
