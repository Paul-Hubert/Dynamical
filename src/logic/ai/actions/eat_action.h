#ifndef EAT_ACTION_H
#define EAT_ACTION_H

#include "action.h"

namespace dy {

class EatAction : public Action {
public:
    EatAction(entt::registry& reg, entt::entity entity) : Action(reg, entity) {}
    static float getScore(entt::registry& reg, entt::entity entity);
    void act(const Position position) override;
private:
    void findFood(const Position position);
    
    int phase = 0;
};

}

#endif
