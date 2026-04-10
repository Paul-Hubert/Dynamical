#include "recipe_registry.h"
#include <logic/components/time.h>

using namespace dy;

void RecipeRegistry::initialize() {
    recipes_ = {
        {
            "plank",
            {{Item::wood, 1}},
            Item::plank, 2,
            5.0f * Time::minute
        },
        {
            "wooden_sword",
            {{Item::wood, 3}},
            Item::wooden_sword, 1,
            10.0f * Time::minute
        },
        {
            "stone_pickaxe",
            {{Item::stone, 2}, {Item::wood, 1}},
            Item::stone_pickaxe, 1,
            10.0f * Time::minute
        },
        {
            "cooked_meat",
            {{Item::meat, 1}, {Item::wood, 1}},
            Item::cooked_meat, 1,
            5.0f * Time::minute
        },
        {
            "cooked_fish",
            {{Item::fish, 1}, {Item::wood, 1}},
            Item::cooked_fish, 1,
            5.0f * Time::minute
        },
    };
}

const CraftingRecipe* RecipeRegistry::find(const std::string& name) const {
    for (auto& r : recipes_) {
        if (r.name == name) return &r;
    }
    return nullptr;
}
