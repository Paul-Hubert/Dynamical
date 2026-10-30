#ifndef EDIBLE_H
#define EDIBLE_H

#include <unordered_map>

#include "item.h"

namespace dy {

class Edible {
public:
    static float getNutrition(Item::ID id);
};

}

#endif

