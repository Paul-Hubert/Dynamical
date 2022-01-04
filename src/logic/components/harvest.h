#ifndef HARVEST_H
#define HARVEST_H

#include <entt/entt.hpp>

#include <logic/components/time.h>

class Harvest {
public:
    Harvest(entt::registry& reg, entt::entity plant) : plant(plant) {
        start = reg.ctx<dy::Time>().current;
    }
    entt::entity plant;
    time_t start;
};

#endif
