#include "harvest_action.h"

#include <extra/optick/optick.h>

#include "logic/map/map_manager.h"

#include "util/log.h"

#include "logic/components/path.h"
#include <logic/components/object.h>
#include <logic/components/storage.h>
#include <logic/components/harvest.h>
#include <logic/components/harvested.h>

using namespace entt::literals;

using namespace dy;

HarvestAction::HarvestAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params)
{
    machine_
        .stage("finding plant", [this](double) -> StageStatus {
            if (this->reg.all_of<Path>(this->entity))
                this->reg.remove<Path>(this->entity);

            find();

            if (target_ == entt::null) {
                this->failure_reason = "no berry bushes available";
                return StageStatus::Failed;
            }

            this->reg.emplace<entt::tag<"reserved"_hs>>(target_);
            return StageStatus::Complete;
        })
        .stage("navigating to plant",
            stage_primitives::wait_until_removed<Path>(reg, entity))
        .stage("begin harvesting", [this](double) -> StageStatus {
            this->reg.emplace<Harvest>(this->entity, this->reg, target_);
            return StageStatus::Complete;
        })
        .stage("harvesting",
            stage_primitives::wait_until_removed<Harvest>(reg, entity));
}

std::unique_ptr<Action> HarvestAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();

    StageStatus status = machine_.tick(dt);

    if (status == StageStatus::Complete) {
        reg.remove<entt::tag<"reserved"_hs>>(target_);
        return nullptr;
    }

    if (status == StageStatus::Failed) {
        return nullptr;
    }

    return self;
}

std::string HarvestAction::describe() const {
    return "Harvest: " + machine_.current_stage_name();
}

void HarvestAction::find() {

    auto& map = reg.ctx<MapManager>();

    auto position = reg.get<Position>(entity);

    target_ = entt::null;

    reg.emplace<Path>(entity, map.pathfind(position, [&](glm::vec2 pos) {
        auto object = map.getTile(pos)->object;
        if (object != entt::null && reg.all_of<Object>(object) && reg.get<Object>(object).id == plant_ && !reg.all_of<entt::tag<"reserved"_hs>>(object) && !reg.all_of<Harvested>(object)) {
            target_ = object;
            return true;
        }
        return false;
    }));

}
