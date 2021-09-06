#ifndef HARVEST_ACTION_H
#define HARVEST_ACTION_H

#include "action.h"

#include <logic/components/plant.h>

namespace dy {

class HarvestAction : public Action {
public:
    HarvestAction(entt::registry& reg, entt::entity entity) : Action(reg, entity) {}
    std::unique_ptr<Action> deploy(std::unique_ptr<Action> self, Plant::Type plant);
    std::unique_ptr<Action> act(std::unique_ptr<Action> self) override;
private:
    void find();
    
    Plant::Type plant;
    int phase = 0;
    entt::entity target = entt::null;
};

}

#endif
