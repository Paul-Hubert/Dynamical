#ifndef EAT_BEHAVIOR_H
#define EAT_BEHAVIOR_H

#include "behavior.h"

namespace dy {

class EatBehavior : public Behavior {
public:
    EatBehavior(entt::registry& reg, entt::entity entity) : Behavior(reg, entity) {}
    static float getScore(entt::registry& reg, entt::entity entity);
    std::unique_ptr<Action> action() override;
};

}

#endif
