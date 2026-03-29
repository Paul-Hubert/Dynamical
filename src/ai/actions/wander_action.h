#ifndef WANDER_ACTION_H
#define WANDER_ACTION_H

#include <ai/action_base.h>

namespace dy {

class WanderAction : public ActionBase<WanderAction> {
public:
    WanderAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {})
        : ActionBase(reg, entity, params) {
        interruptible = true;
    }

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override { return "wandering"; }
};

}

#endif
