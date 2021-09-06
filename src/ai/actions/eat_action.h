#ifndef EAT_ACTION_H
#define EAT_ACTION_H

#include "action.h"

#include <logic/components/item.h>

namespace dy {

class EatAction : public Action {
public:
    EatAction(entt::registry& reg, entt::entity entity) : Action(reg, entity) {}
    std::unique_ptr<Action> deploy(std::unique_ptr<Action> self);
    std::unique_ptr<Action> act(std::unique_ptr<Action> self) override;
private:
    void find();
    int phase = 0;
    Item::ID food = Item::null;
    
};

}

#endif
