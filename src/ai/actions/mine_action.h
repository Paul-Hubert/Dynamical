#ifndef MINE_ACTION_H
#define MINE_ACTION_H

#include <ai/action_base.h>

namespace dy {

class MineAction : public ActionBase<MineAction> {
public:
    MineAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {})
        : ActionBase(reg, entity, params) {}

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
};

}

#endif
