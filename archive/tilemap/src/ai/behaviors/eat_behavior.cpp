#include "eat_behavior.h"

#include <ai/actions/eat_action.h>

#include <logic/components/basic_needs.h>

using namespace dy;

float EatBehavior::getScore(entt::registry& reg, entt::entity entity) {
    float hunger = reg.get<BasicNeeds>(entity).hunger;
    return hunger > 5 ? hunger : 0;
}

std::unique_ptr<Action> EatBehavior::action() {
    
    std::unique_ptr<EatAction> top = std::make_unique<EatAction>(reg, entity);
    
    return top->deploy(std::move(top));
    
}
