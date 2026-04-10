#ifndef CRAFTING_H
#define CRAFTING_H

#include <entt/entt.hpp>
#include <string>
#include <logic/components/time.h>

namespace dy {

class Crafting {
public:
    Crafting(entt::registry& reg, const std::string& recipe_name)
        : recipe_name(recipe_name)
    {
        start = reg.ctx<dy::Time>().current;
    }
    std::string recipe_name;
    time_t start;
};

}

#endif
