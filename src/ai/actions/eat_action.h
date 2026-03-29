#ifndef EAT_ACTION_H
#define EAT_ACTION_H

#include <ai/action_base.h>
#include <logic/components/item.h>

namespace dy {

class EatAction : public ActionBase<EatAction> {
public:
    EatAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {})
        : ActionBase(reg, entity, params) {}

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self) override;

private:
    void find();
    int phase = 0;
    Item::ID food = Item::null;
};

}

#endif
