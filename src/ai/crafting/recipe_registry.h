#ifndef RECIPE_REGISTRY_H
#define RECIPE_REGISTRY_H

#include <string>
#include <vector>
#include <utility>
#include <logic/components/item.h>

namespace dy {

struct CraftingRecipe {
    std::string name;
    std::vector<std::pair<Item::ID, int>> inputs;
    Item::ID output;
    int output_amount;
    float craft_time;  // game-time seconds
};

class RecipeRegistry {
public:
    static RecipeRegistry& instance() {
        static RecipeRegistry registry;
        return registry;
    }

    void initialize();
    const CraftingRecipe* find(const std::string& name) const;
    const std::vector<CraftingRecipe>& all() const { return recipes_; }

private:
    RecipeRegistry() = default;
    std::vector<CraftingRecipe> recipes_;
};

}

#endif
