#ifndef CHOPPING_H
#define CHOPPING_H

#include <entt/entt.hpp>

#include <logic/components/time.h>

class Chopping {
public:
    Chopping(entt::registry& reg, entt::entity tree) : tree(tree) {
        start = reg.ctx<dy::Time>().current;
    }
    entt::entity tree;
    time_t start;
};

#endif
