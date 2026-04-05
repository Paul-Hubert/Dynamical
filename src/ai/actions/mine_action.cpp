#include "mine_action.h"

#include <extra/optick/optick.h>

#include "logic/map/map_manager.h"

#include "logic/components/path.h"
#include <logic/components/storage.h>
#include <logic/components/mining.h>

using namespace entt::literals;

using namespace dy;

MineAction::MineAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params)
{
    machine_
        .stage("finding stone", [this](double) -> StageStatus {
            if (this->reg.all_of<Path>(this->entity))
                this->reg.remove<Path>(this->entity);

            find();

            if (stone_tile_ == glm::vec2{0, 0}) {
                this->failure_reason = "no stone tiles nearby";
                return StageStatus::Failed;
            }

            return StageStatus::Complete;
        })
        .stage("navigating to stone",
            stage_primitives::wait_until_removed<Path>(reg, entity))
        .stage("begin mining", [this](double) -> StageStatus {
            this->reg.emplace<Mining>(this->entity, this->reg, stone_tile_);
            return StageStatus::Complete;
        })
        .stage("mining",
            stage_primitives::wait_until_removed<Mining>(reg, entity));
}

std::unique_ptr<Action> MineAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();

    StageStatus status = machine_.tick(dt);

    if (status == StageStatus::Complete || status == StageStatus::Failed) {
        return nullptr;
    }

    return self;
}

std::string MineAction::describe() const {
    return "Mine: " + machine_.current_stage_name();
}

void MineAction::find() {
    auto& map = reg.ctx<MapManager>();
    auto position = reg.get<Position>(entity);

    stone_tile_ = {0, 0};

    reg.emplace<Path>(entity, map.pathfind(position, [&](glm::vec2 pos) {
        // Check if any adjacent tile is stone terrain
        for (int i = -1; i <= 1; i++) {
            for (int j = -1; j <= 1; j++) {
                if (i == 0 && j == 0) continue;
                glm::vec2 adj = pos + glm::vec2(i, j);
                auto* tile = map.getTile(adj);
                if (tile && tile->terrain == Tile::stone) {
                    stone_tile_ = adj;
                    return true;
                }
            }
        }
        return false;
    }));
}
