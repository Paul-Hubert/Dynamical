#include "system_list.h"

#include <logic/components/crafting.h>
#include <logic/components/storage.h>
#include <logic/components/item.h>
#include <logic/components/action_bar.h>
#include <ai/crafting/recipe_registry.h>

using namespace dy;

void CraftSys::preinit() {}
void CraftSys::init() {}

void CraftSys::tick(double dt) {
    OPTICK_EVENT();

    auto& time = reg.ctx<Time>();

    auto view = reg.view<Crafting>();
    view.each([&](const auto entity, auto& crafting) {
        const CraftingRecipe* recipe = RecipeRegistry::instance().find(crafting.recipe_name);
        if (!recipe) {
            reg.remove<Crafting>(entity);
            if (reg.all_of<ActionBar>(entity)) reg.remove<ActionBar>(entity);
            return;
        }

        time_t duration = static_cast<time_t>(recipe->craft_time);

        if (!reg.all_of<ActionBar>(entity)) {
            reg.emplace<ActionBar>(entity, crafting.start, crafting.start + duration);
        }

        if (time.current > crafting.start + duration) {
            auto& storage = reg.get<Storage>(entity);
            storage.add(Item(recipe->output, recipe->output_amount));

            reg.remove<Crafting>(entity);
            reg.remove<ActionBar>(entity);
        }
    });
}

void CraftSys::finish() {}
