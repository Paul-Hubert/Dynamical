#ifndef EAT_ACTION_H
#define EAT_ACTION_H

#include "action.h"

class EatAction : public Action {
public:
    EatAction(entt::registry& reg, entt::entity entity) : Action(reg, entity) {}
    static float getScore(entt::registry& reg, entt::entity entity);
    void act(const PositionC position) override;
private:
    void findFood(const PositionC position);
    
    int phase = 0;
};

#endif
