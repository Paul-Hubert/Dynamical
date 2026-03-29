#ifndef CRAFT_ACTION_H
#define CRAFT_ACTION_H

#include <ai/action_base.h>

namespace dy {

class CraftAction : public ActionBase<CraftAction> {
public:
    CraftAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {})
        : ActionBase(reg, entity, params) {}

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self) override;
};

}

#endif
