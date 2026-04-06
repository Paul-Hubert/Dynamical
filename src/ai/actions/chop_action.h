#ifndef CHOP_ACTION_H
#define CHOP_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>
#include <logic/components/object.h>

namespace dy {

class ChopAction : public ActionBase<ChopAction> {
public:
    ChopAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    void find();

    entt::entity target_ = entt::null;
    ActionStageMachine machine_;
};

}

#endif
