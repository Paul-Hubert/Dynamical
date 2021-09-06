#ifndef WANDER_ACTION_H
#define WANDER_ACTION_H

#include "action.h"

namespace dy {
    
class WanderAction : public Action {
public:
    WanderAction(entt::registry& reg, entt::entity entity) : Action(reg, entity) {interruptible = true;}
    std::unique_ptr<Action> deploy(std::unique_ptr<Action> self);
    std::unique_ptr<Action> act(std::unique_ptr<Action> self) override;
};

}

#endif
