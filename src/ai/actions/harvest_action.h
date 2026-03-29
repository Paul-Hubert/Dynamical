#ifndef HARVEST_ACTION_H
#define HARVEST_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>
#include <logic/components/object.h>

namespace dy {

class HarvestAction : public ActionBase<HarvestAction> {
public:
    HarvestAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    void find();

    Object::Identifier plant_ = Object::berry_bush;  // Default; will be set from ActionParams.target_type
    entt::entity target_ = entt::null;
    ActionStageMachine machine_;
};

}

#endif
