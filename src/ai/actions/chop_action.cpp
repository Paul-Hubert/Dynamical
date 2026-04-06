#include "chop_action.h"

#include <extra/optick/optick.h>

#include "logic/map/map_manager.h"

#include "logic/components/path.h"
#include <logic/components/object.h>
#include <logic/components/chopping.h>

using namespace entt::literals;

using namespace dy;

ChopAction::ChopAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params)
{
    machine_
        .stage("finding tree", [this](double) -> StageStatus {
            if (this->reg.all_of<Path>(this->entity))
                this->reg.remove<Path>(this->entity);

            find();

            if (target_ == entt::null) {
                this->failure_reason = "no trees available";
                return StageStatus::Failed;
            }

            this->reg.emplace<entt::tag<"reserved"_hs>>(target_);
            return StageStatus::Complete;
        })
        .stage("navigating to tree",
            stage_primitives::wait_until_removed<Path>(reg, entity))
        .stage("begin chopping", [this](double) -> StageStatus {
            this->reg.emplace<Chopping>(this->entity, this->reg, target_);
            return StageStatus::Complete;
        })
        .stage("chopping",
            stage_primitives::wait_until_removed<Chopping>(reg, entity));
}

std::unique_ptr<Action> ChopAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();

    StageStatus status = machine_.tick(dt);

    if (status == StageStatus::Complete) {
        if (target_ != entt::null && reg.valid(target_) && reg.all_of<entt::tag<"reserved"_hs>>(target_))
            reg.remove<entt::tag<"reserved"_hs>>(target_);
        return nullptr;
    }

    if (status == StageStatus::Failed) {
        if (target_ != entt::null && reg.valid(target_) && reg.all_of<entt::tag<"reserved"_hs>>(target_))
            reg.remove<entt::tag<"reserved"_hs>>(target_);
        return nullptr;
    }

    return self;
}

std::string ChopAction::describe() const {
    return "Chop: " + machine_.current_stage_name();
}

void ChopAction::find() {

    auto& map = reg.ctx<MapManager>();

    auto position = reg.get<Position>(entity);

    target_ = entt::null;

    reg.emplace<Path>(entity, map.pathfind(position, [&](glm::vec2 pos) {
        auto object = map.getTile(pos)->object;
        if (object != entt::null && reg.all_of<Object>(object) && reg.get<Object>(object).id == Object::tree && !reg.all_of<entt::tag<"reserved"_hs>>(object)) {
            target_ = object;
            return true;
        }
        return false;
    }));

}
