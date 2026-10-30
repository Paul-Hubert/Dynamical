#ifndef WANDER_BEHAVIOR_H
#define WANDER_BEHAVIOR_H

#include "behavior.h"

namespace dy {

class WanderBehavior : public Behavior {
public:
    WanderBehavior(entt::registry& reg, entt::entity entity) : Behavior(reg, entity) {}
    static float getScore(entt::registry& reg, entt::entity entity) {return 1;}
    std::unique_ptr<Action> action() override;
    
};

}

#endif
