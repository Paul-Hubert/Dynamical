#include "water_flow.h"

#include "logic/components/water_flow.h"

using namespace dy;

void fillWater(entt::registry& reg, MapManager& map, entt::entity entity, WaterFlow& flow) {

    if(!flow.queue.empty()) {

        auto min = flow.queue.top();
        flow.queue.pop();

        Tile* lowest_tile = map.getTile(min.pos);



        lowest_tile->terrain = Tile::shallow_water;

        map.getChunk(map.getChunkPos(min.pos))->setUpdated();

        for(int x = -1; x<=1; x++) {
            for(int y = -1; y<=1; y++) {
                if(abs(x) + abs(y) != 1) continue;
                glm::vec2 adj = min.pos + glm::vec2(x, y);

                Tile* tile = map.getTile(adj);

                if(!tile) {
                    map.generateChunk(map.getChunkPos(adj));
                    tile = map.getTile(adj);
                }

                if(tile->terrain == Tile::shallow_water) continue;
                if(tile->terrain == Tile::water) {
                    reg.remove<WaterFlow>(entity);
                    reg.destroy(entity);
                    return;
                }

                flow.queue.emplace(adj, tile->level);

            }
        }

    } else {
        reg.remove<WaterFlow>(entity);
        reg.destroy(entity);
        return;
    }

}

void WaterFlowSys::tick(float dt) {

    auto& map = reg.ctx<MapManager>();

    auto view = reg.view<WaterFlow>();

    for(auto entity : view) {

        auto& flow = view.get<WaterFlow>(entity);

        fillWater(reg, map, entity, flow);

    }

}