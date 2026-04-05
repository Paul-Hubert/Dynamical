#include "system_list.h"

#include "logic/components/time.h"
#include "logic/components/basic_needs.h"
#include "logic/components/position.h"
#include "logic/components/building.h"
#include "logic/map/map_manager.h"
#include "logic/map/tile.h"

using namespace dy;

void BasicNeedsSys::preinit() {

}

void BasicNeedsSys::init() {

}

void BasicNeedsSys::tick(double dt) {

    OPTICK_EVENT();

    auto& time = reg.ctx<Time>();
    auto& map = reg.ctx<MapManager>();

    auto view = reg.view<BasicNeeds>();

    view.each([&](entt::entity entity, BasicNeeds& needs) {
        // Base hunger increase
        double hunger_increase = 10.0 / (Date::seconds_in_a_minute * Date::minutes_in_an_hour * Date::hours_in_a_day) * time.dt;

        // Shelter bonus: entities in complete buildings get 50% reduced hunger drain
        if (reg.all_of<Position>(entity)) {
            const auto& pos = reg.get<Position>(entity);
            if (auto* tile = map.getTile(glm::vec2(pos.x, pos.y))) {
                // Check if on a building tile
                if (tile->building_role == Tile::floor_tile || tile->building_role == Tile::door_tile) {
                    if (tile->building != entt::null && reg.valid(tile->building)) {
                        if (reg.all_of<Building>(tile->building)) {
                            auto& building = reg.get<Building>(tile->building);
                            // Check if building is complete and entity is a resident
                            if (building.complete) {
                                auto it = std::find(building.residents.begin(), building.residents.end(), entity);
                                if (it != building.residents.end()) {
                                    hunger_increase *= 0.5;  // 50% reduction
                                }
                            }
                        }
                    }
                }
            }
        }

        needs.hunger += hunger_increase;
    });

}

void BasicNeedsSys::finish() {
    
}
