#ifndef EAT_H
#define EAT_H

#include <entt/entt.hpp>

#include <logic/components/time.h>

#include "item.h"

namespace dy {

class Eat {
public:
    Eat(entt::registry& reg, Item::ID food) : food(food) {
        start = reg.ctx<dy::Time>().current;
    }
    Item::ID food;
    time_t start;
};

}

#endif
