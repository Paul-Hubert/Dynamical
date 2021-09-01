#ifndef WANDER_ACTION_H
#define WANDER_ACTION_H

#include "action.h"

namespace dy {
    
class WanderAction : public Action {
public:
    WanderAction(entt::registry& reg, entt::entity entity) : Action(reg, entity) {interruptible = true;}
    static float getScore(entt::registry& reg, entt::entity entity);
    void act(const Position position) override;
};

}

#endif
