#ifndef SLEEP_ACTION_H
#define SLEEP_ACTION_H

#include <ai/action_base.h>

namespace dy {

class SleepAction : public ActionBase<SleepAction> {
public:
    SleepAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {})
        : ActionBase(reg, entity, params) {}

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
};

}

#endif
