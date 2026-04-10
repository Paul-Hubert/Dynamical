#include "craft_action.h"

#include <extra/optick/optick.h>

#include "util/log.h"
#include "logic/components/storage.h"
#include "logic/components/item.h"
#include "logic/components/crafting.h"
#include "ai/crafting/recipe_registry.h"
#include "ai/memory/ai_memory.h"

using namespace dy;

CraftAction::CraftAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params)
{
    machine_
        .stage("checking recipe", [this](double) -> StageStatus {
            const CraftingRecipe* recipe = RecipeRegistry::instance().find(this->params.recipe);
            if (!recipe) {
                this->failure_reason = "unknown recipe: " + this->params.recipe;
                return StageStatus::Failed;
            }

            if (!this->reg.all_of<Storage>(this->entity)) {
                this->failure_reason = "no storage";
                return StageStatus::Failed;
            }

            auto& storage = this->reg.get<Storage>(this->entity);
            for (auto& [item_id, amount] : recipe->inputs) {
                if (storage.amount(item_id) < amount) {
                    this->failure_reason = "missing " + std::string(Item::to_string(item_id))
                        + " x" + std::to_string(amount);
                    return StageStatus::Failed;
                }
            }
            recipe_ = recipe;
            return StageStatus::Complete;
        })
        .stage("consuming ingredients", [this](double) -> StageStatus {
            auto& storage = this->reg.get<Storage>(this->entity);
            for (auto& [item_id, amount] : recipe_->inputs) {
                storage.remove(Item(item_id, amount));
            }
            this->reg.emplace<Crafting>(this->entity, this->reg, recipe_->name);
            return StageStatus::Complete;
        })
        .stage("crafting",
            stage_primitives::wait_until_removed<Crafting>(reg, entity));
}

std::unique_ptr<Action> CraftAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();

    StageStatus status = machine_.tick(dt);

    if (status == StageStatus::Complete) {
        if (this->reg.all_of<AIMemory>(this->entity)) {
            auto& memory = this->reg.get<AIMemory>(this->entity);
            memory.add_event("action_completed", "Crafted " + this->params.recipe);
        }
        return nullptr;
    }

    if (status == StageStatus::Failed) {
        return nullptr;
    }

    return self;
}

std::string CraftAction::describe() const {
    return "Craft: " + machine_.current_stage_name();
}
