#ifndef HARVEST_ACTION_H
#define HARVEST_ACTION_H

#include <ai/action_base.h>
#include <logic/components/object.h>

namespace dy {

class HarvestAction : public ActionBase<HarvestAction> {
public:
    HarvestAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {})
        : ActionBase(reg, entity, params) {}

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self) override;

private:
    void find();

    Object::Identifier plant = Object::tree;  // Default; will be set from ActionParams.target_type
    int phase = 0;
    entt::entity target = entt::null;
};

}

#endif
