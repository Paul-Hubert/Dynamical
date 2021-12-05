#ifndef WATER_FLOW_H
#define WATER_FLOW_H

#include <entt/entt.hpp>
#include <queue>

#include "logic/map/map_manager.h"

namespace dy {

class WaterFlow {
public:
    WaterFlow(entt::registry& reg, glm::vec2 pos) {
        auto& map = reg.ctx<MapManager>();
        Tile* tile = map.getTile(pos);
        queue.emplace(pos, tile->level);
    }
    struct PriorityElement {
        PriorityElement(glm::vec2 pos, float level) : pos(pos), level(level) {}
        PriorityElement() {
            level = std::numeric_limits<float>::max();
        }
        glm::vec2 pos;
        float level;
    };

    std::priority_queue<PriorityElement, std::vector<PriorityElement>> queue;
};

inline bool operator < (const WaterFlow::PriorityElement a, const WaterFlow::PriorityElement b) {
    return a.level > b.level;
}

}

#endif
