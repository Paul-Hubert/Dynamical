#include "eat_action.h"

#include <extra/optick/optick.h>

#include "logic/map/map_manager.h"

#include "util/log.h"

#include "harvest_action.h"

#include "logic/components/path.h"
#include <logic/components/storage.h>
#include <logic/components/eat.h>
#include <logic/components/item.h>

using namespace entt::literals;

using namespace dy;

EatAction::EatAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params)
{
    machine_
        .stage("finding food", [this](double) -> StageStatus {
            find();
            if (food_ == Item::null) {
                this->failure_reason = "no food in inventory";
                return StageStatus::Failed;
            }
            this->reg.emplace<Eat>(this->entity, this->reg, food_);
            return StageStatus::Complete;
        })
        .stage("eating",
            stage_primitives::wait_until_removed<Eat>(reg, entity));
}

std::unique_ptr<Action> EatAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();

    StageStatus status = machine_.tick(dt);
    return (status == StageStatus::Continue) ? std::move(self) : nullptr;
}

std::string EatAction::describe() const {
    return "Eat: " + machine_.current_stage_name();
}

void EatAction::find() {

    const Storage& storage = this->reg.get<Storage>(this->entity);

    food_ = Item::null;

    if (storage.amount(Item::berry) > 0) {
        food_ = Item::berry;
    }

}
