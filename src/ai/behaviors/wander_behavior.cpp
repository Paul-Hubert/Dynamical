#include "wander_behavior.h"

#include <ai/actions/wander_action.h>

using namespace dy;

std::unique_ptr<Action> WanderBehavior::action() {
    
    auto action = std::make_unique<WanderAction>(reg, entity);
    
    return action->deploy(std::move(action));
    
}
