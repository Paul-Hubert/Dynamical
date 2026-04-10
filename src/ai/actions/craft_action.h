#ifndef CRAFT_ACTION_H
#define CRAFT_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>

namespace dy {

struct CraftingRecipe;

class CraftAction : public ActionBase<CraftAction> {
public:
    CraftAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});
    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    const CraftingRecipe* recipe_ = nullptr;
    ActionStageMachine machine_;
};

}

#endif
