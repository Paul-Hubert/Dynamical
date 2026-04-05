#include "build_action.h"

#include <extra/optick/optick.h>

#include "logic/map/map_manager.h"
#include "logic/components/storage.h"
#include "logic/components/item.h"
#include "logic/components/path.h"
#include "logic/components/position.h"
#include "logic/components/renderable.h"
#include "logic/components/object.h"
#include "logic/components/construction.h"
#include "ai/memory/ai_memory.h"

using namespace entt::literals;
using namespace dy;

BuildAction::BuildAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params)
{
    machine_
        .stage("checking resources", [this, &params = this->params](double) -> StageStatus {
            // Parse structure type from params
            if (params.structure.empty()) {
                this->failure_reason = "unknown building type";
                return StageStatus::Failed;
            }

            // Find matching building template
            building_template_ = nullptr;
            for (const auto& tmpl : get_building_templates()) {
                if (tmpl.name == params.structure) {
                    building_template_ = &tmpl;
                    break;
                }
            }

            if (building_template_ == nullptr) {
                this->failure_reason = "unknown building type";
                return StageStatus::Failed;
            }

            // Check storage has enough materials
            if (!this->reg.all_of<Storage>(this->entity)) {
                this->failure_reason = "no storage";
                return StageStatus::Failed;
            }

            auto& storage = this->reg.get<Storage>(this->entity);
            int wood_needed = building_template_->wood_cost();
            int stone_needed = building_template_->stone_cost();

            int wood_have = storage.amount(Item::wood);
            int stone_have = storage.amount(Item::stone);

            if (wood_have < wood_needed) {
                this->failure_reason = "need " + std::to_string(wood_needed - wood_have) + " more wood";
                return StageStatus::Failed;
            }

            if (stone_have < stone_needed) {
                this->failure_reason = "need " + std::to_string(stone_needed - stone_have) + " more stone";
                return StageStatus::Failed;
            }

            return StageStatus::Complete;
        })
        .stage("finding site", [this](double) -> StageStatus {
            auto& map = this->reg.ctx<MapManager>();
            auto position = this->reg.get<Position>(this->entity);

            placement_result_ = find_building_site(this->reg, map, glm::vec2(position.x, position.y), *building_template_);

            if (!placement_result_.success) {
                this->failure_reason = "no suitable building location nearby";
                return StageStatus::Failed;
            }

            // Remove trees/bushes on the footprint
            for (const auto& tile_pos : placement_result_.tiles) {
                auto* tile = map.getTile(glm::vec2(tile_pos));
                if (tile && tile->object != entt::null && this->reg.all_of<Object>(tile->object)) {
                    auto& obj = this->reg.get<Object>(tile->object);
                    if (obj.type == Object::plant) {
                        this->reg.destroy(tile->object);
                        tile->object = entt::null;
                        auto chunk = map.getTileChunk(glm::vec2(tile_pos));
                        if (chunk) chunk->setUpdated(true);
                    }
                }
            }

            // Reserve tiles
            for (const auto& tile_pos : placement_result_.tiles) {
                auto* tile = map.getTile(glm::vec2(tile_pos));
                if (tile) {
                    tile->building_role = Tile::floor_tile;
                }
            }

            return StageStatus::Complete;
        })
        .stage("navigating to site", [this](double) -> StageStatus {
            auto& map = this->reg.ctx<MapManager>();

            if (this->reg.all_of<Path>(this->entity)) {
                this->reg.remove<Path>(this->entity);
            }

            // Navigate to door position
            glm::vec2 door_world_pos(placement_result_.door_position);
            auto position = this->reg.get<Position>(this->entity);

            this->reg.emplace<Path>(
                this->entity,
                map.pathfind(glm::vec2(position.x, position.y), [&](glm::vec2 pos) {
                    return pos == door_world_pos;
                })
            );

            return StageStatus::Complete;
        })
        .stage("waiting for navigation", stage_primitives::wait_until_removed<Path>(reg, entity))
        .stage("constructing", [this, &params = this->params](double) -> StageStatus {
            // Deduct materials
            auto& storage = this->reg.get<Storage>(this->entity);
            storage.remove(Item{Item::wood, building_template_->wood_cost()});
            storage.remove(Item{Item::stone, building_template_->stone_cost()});

            // Create building entity
            auto& map = this->reg.ctx<MapManager>();

            // Calculate center position
            glm::vec2 origin_world(placement_result_.origin);
            glm::vec2 footprint_center = origin_world + glm::vec2(building_template_->footprint) * 0.5f;

            // Get average height
            float avg_height = 0.0f;
            int count = 0;
            for (const auto& tile_pos : placement_result_.tiles) {
                if (auto* tile = map.getTile(glm::vec2(tile_pos))) {
                    avg_height += tile->level;
                    count++;
                }
            }
            if (count > 0) avg_height /= count;

            // Create building entity
            building_entity_ = this->reg.create();
            this->reg.emplace<Building>(
                building_entity_,
                Building{
                    .type = static_cast<Building::Type>(params.structure == "small_building" ? 0 :
                                                        params.structure == "medium_building" ? 1 : 2),
                    .owner = this->entity,
                    .residents = {this->entity},
                    .occupied_tiles = placement_result_.tiles,
                    .door_tile = placement_result_.door_position,
                    .construction_progress = 0.0f,
                    .complete = false
                }
            );

            this->reg.emplace<Position>(building_entity_, footprint_center, avg_height);
            auto& renderable = this->reg.emplace<Renderable>(building_entity_);
            renderable.color = Color("8B6914");  // Brown wood color

            // Insert into map
            map.insert(building_entity_, footprint_center);

            // Mark tiles as occupied
            for (const auto& tile_pos : placement_result_.tiles) {
                auto* tile = map.getTile(glm::vec2(tile_pos));
                if (tile) {
                    tile->building = building_entity_;
                    // Determine role: door vs floor
                    if (tile_pos == placement_result_.door_position) {
                        tile->building_role = Tile::door_tile;
                    } else {
                        tile->building_role = Tile::floor_tile;
                    }
                }
            }

            // Tag builder as constructing
            this->reg.emplace<entt::tag<"constructing"_hs>>(this->entity);

            return StageStatus::Complete;
        })
        .stage("building", [this, &params = this->params](double dt) -> StageStatus {
            auto& building = this->reg.get<Building>(building_entity_);

            building.construction_progress += static_cast<float>(dt) * 0.2f;  // 5 seconds to complete

            if (building.construction_progress >= 1.0f) {
                building.construction_progress = 1.0f;
                building.complete = true;

                // Remove construction tag
                if (this->reg.all_of<entt::tag<"constructing"_hs>>(this->entity)) {
                    this->reg.remove<entt::tag<"constructing"_hs>>(this->entity);
                }

                // Record memory event
                if (this->reg.all_of<AIMemory>(this->entity)) {
                    auto& memory = this->reg.get<AIMemory>(this->entity);
                    glm::vec2 center(placement_result_.origin);
                    memory.add_event("action_completed", "Built a " + params.structure + " at (" +
                                   std::to_string(static_cast<int>(center.x)) + "," +
                                   std::to_string(static_cast<int>(center.y)) + ")");
                }

                return StageStatus::Complete;
            }

            return StageStatus::Continue;
        });
}

std::unique_ptr<Action> BuildAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();

    StageStatus status = machine_.tick(dt);

    if (status == StageStatus::Complete) {
        return nullptr;
    }

    if (status == StageStatus::Failed) {
        cleanup_reserved_tiles();
        return nullptr;
    }

    return self;
}

std::string BuildAction::describe() const {
    return "Build: " + machine_.current_stage_name();
}

void BuildAction::cleanup_reserved_tiles() {
    auto& map = this->reg.ctx<MapManager>();

    for (const auto& tile_pos : placement_result_.tiles) {
        auto* tile = map.getTile(glm::vec2(tile_pos));
        if (tile && tile->building_role == Tile::floor_tile && tile->building == entt::null) {
            tile->building_role = Tile::no_building;
        }
    }
}
