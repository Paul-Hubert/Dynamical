#ifndef HARVEST_ACTION_H
#define HARVEST_ACTION_H

#include "action.h"

#include "logic/components/object.h"

namespace dy {

class HarvestAction : public Action {
public:
    HarvestAction(entt::registry& reg, entt::entity entity) : Action(reg, entity) {}
    std::unique_ptr<Action> deploy(std::unique_ptr<Action> self, Object::Identifier plant);
    std::unique_ptr<Action> act(std::unique_ptr<Action> self) override;
private:
    void find();
    
    Object::Identifier plant;
    int phase = 0;
    entt::entity target = entt::null;
};

}

#endif
