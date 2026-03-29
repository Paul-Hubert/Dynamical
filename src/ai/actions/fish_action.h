#ifndef FISH_ACTION_H
#define FISH_ACTION_H

#include <ai/action_base.h>

namespace dy {

class FishAction : public ActionBase<FishAction> {
public:
    FishAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {})
        : ActionBase(reg, entity, params) {}

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self) override;
};

}

#endif
