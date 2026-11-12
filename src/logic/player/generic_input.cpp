#include "generic_input.h"

#include <algorithm>

using namespace dy;

glm::vec2 MouseKeyboardInput::getMovementAxis() const {
    glm::vec2 move(0.f);
    if(keys[W]) move.y += 1.f;
    if(keys[S]) move.y -= 1.f;
    if(keys[D]) move.x += 1.f;
    if(keys[A]) move.x -= 1.f;
    float length = glm::length(move);
    return length > 0.f ? move / length : move;
}

glm::vec2 ControllersInput::stick(glm::vec2 raw) const {
    if(!connected) return glm::vec2(0.f);
    float length = glm::length(raw);
    if(length <= deadzone) return glm::vec2(0.f);
    return raw / length * std::min((length - deadzone) / std::max(1.f - deadzone, 1e-3f), 1.f);
}

glm::vec2 VRPlayerInput::getMovementAxis() const {
    return focused && glm::length(left_stick) > 0.5f ? glm::normalize(left_stick) : glm::vec2(0.f);
}

glm::vec2 VRPlayerInput::getViewAxis() const {
    return focused && glm::length(right_stick) > 0.5f ? right_stick : glm::vec2(0.f);
}

namespace {

struct NoInput : GenericPlayerInput {
    glm::vec2 getMovementAxis() const override { return glm::vec2(0.f); }
    glm::vec2 getViewAxis() const override { return glm::vec2(0.f); }
    bool getJumpButton() const override { return false; }
    bool getPrimaryActionButton() const override { return false; }
    bool getSecondaryActionButton() const override { return false; }
    bool getInteractButton() const override { return false; }
    bool getRunButton() const override { return false; }
    float getControlAxis() const override { return 0.f; }
    bool isActive() const override { return false; }
};

}

const GenericPlayerInput& dy::playerInput(const entt::registry& reg, entt::entity entity) {

    static const NoInput none;
    if(entity == entt::null || !reg.valid(entity)) return none;
    if(auto* scripted = reg.try_get<ScriptedInput>(entity)) return *scripted;

    const GenericPlayerInput* devices[3] = { reg.try_get<MouseKeyboardInput>(entity), reg.try_get<ControllersInput>(entity), reg.try_get<VRPlayerInput>(entity) };
    const GenericPlayerInput* best = nullptr;
    for(auto* device : devices) {
        if(!device || !device->isActive()) continue;
        if(!best || device->last_action > best->last_action) best = device;
    }
    return best ? *best : none;

}
