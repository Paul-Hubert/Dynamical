#include "water_flow.h"

#include "logic/components/water_flow.h"
#include "logic/components/object.h"
#include "logic/factories/factory_list.h"

using namespace dy;

void floodWater(entt::registry& reg, MapManager& map, glm::vec2 pos, Tile* tile) {

    tile->terrain = Tile::river;

    if(tile->object != entt::null && reg.get<Object>(tile->object).type == Object::plant) {
        dy::destroyObject(reg, tile->object);
        tile->object = entt::null;
    }

    map.getChunk(map.getChunkPos(pos))->setUpdated();
}

bool fillNeighbours(entt::registry& reg, MapManager& map, WaterFlow& flow, glm::vec2 pos) {

    for(int x = -1; x<=1; x++) {
        for(int y = -1; y<=1; y++) {
            if(abs(x) + abs(y) != 1) continue;
            glm::vec2 adj = pos + glm::vec2(x, y);

            if(flow.set.find(adj) != nullptr) {
                continue;
            }

            Tile* tile = map.getTile(adj);

            if(!tile) {
                map.generateChunk(map.getChunkPos(adj));
                tile = map.getTile(adj);
            }

            if(tile->terrain == Tile::water) {
                return false;
            }

            /*
            if(iterations < num_iterations && tile->level < lowest_tile->level + flood_level_multiplier * (flow.start_level - tile->level)) {
                flow.queue.emplace(adj, 0);
                if(!fillWater(reg, map, entity, flow, iterations+1)) {
                    return false;
                }
                continue;
            }
             */

            flow.queue.emplace(adj, tile->level);
            flow.set.emplace(adj);

        }
    }
    return true;

}

bool fillWater(entt::registry& reg, MapManager& map, entt::entity entity, WaterFlow& flow) {

    const float flood_level_multiplier = 0.02f;

    const int num_iterations = 100;

    std::vector<glm::vec2> flooded;

    Tile* lowest_tile = nullptr;

    int iterations = 0;
    while(!flow.queue.empty()) {

        /*
        iterations++;
        if(iterations > num_iterations)
            break;
        */

        auto min = flow.queue.top();

        Tile* tile = map.getTile(min.pos);

        if(!lowest_tile) {
            lowest_tile = tile;
        }

        if(tile->level <= lowest_tile->level + flood_level_multiplier * (flow.start_level - tile->level)) {

        } else {
            //flow.queue = std::priority_queue<WaterFlow::PriorityElement, std::vector<WaterFlow::PriorityElement>>();
            break;
        }

        floodWater(reg, map, min.pos, tile);

        flooded.push_back(min.pos);

        flow.queue.pop();

    }

    if(flooded.empty()) {
        return false;
    }

    bool ret = true;

    for(int i = 0; i<flooded.size(); i++) {

        auto pos = flooded[i];

        if(!fillNeighbours(reg, map, flow, pos)) {
            ret = false;
        }

    }

    return ret;

}

void WaterFlowSys::tick(float dt) {

    auto& map = reg.ctx<MapManager>();

    auto view = reg.view<WaterFlow>();

    for(auto entity : view) {

        auto& flow = view.get<WaterFlow>(entity);

        const auto iterations = 1;

        bool ret = true;

        for(int i = 0; i<iterations&&reg.valid(entity); i++) {
            if(!fillWater(reg, map, entity, flow)) {
                ret = false;
            }
        }

        if(!ret) reg.destroy(entity);



    }

}