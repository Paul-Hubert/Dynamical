#ifndef HUNT_ACTION_H
#define HUNT_ACTION_H

#include <ai/action_base.h>

namespace dy {

class HuntAction : public ActionBase<HuntAction> {
public:
    HuntAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {})
        : ActionBase(reg, entity, params) {}

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self) override;
};

}

#endif
