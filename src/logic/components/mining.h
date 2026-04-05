#ifndef MINING_H
#define MINING_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <logic/components/time.h>

namespace dy {

class Mining {
public:
    Mining(entt::registry& reg, glm::vec2 stone_pos) : stone_tile(stone_pos) {
        start = reg.ctx<dy::Time>().current;
    }
    glm::vec2 stone_tile;
    time_t start;
};

}

#endif
