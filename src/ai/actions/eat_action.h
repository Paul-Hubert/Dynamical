#ifndef EAT_ACTION_H
#define EAT_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>
#include <logic/components/item.h>

namespace dy {

class EatAction : public ActionBase<EatAction> {
public:
    EatAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    void find();
    Item::ID food_ = Item::null;
    ActionStageMachine machine_;
};

}

#endif
