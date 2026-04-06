#include "system_list.h"

#include <logic/components/chopping.h>
#include <logic/components/storage.h>
#include <logic/components/time.h>
#include <logic/components/item.h>
#include <logic/components/action_bar.h>
#include <logic/components/position.h>
#include <logic/map/map_manager.h>
#include <logic/factories/factory_list.h>

using namespace dy;

void ChopSys::preinit() {

}

void ChopSys::init() {

}

void ChopSys::tick(double dt) {

    OPTICK_EVENT();

    constexpr time_t duration = 10 * Time::minute;

    auto& time = reg.ctx<Time>();

    auto view = reg.view<Chopping>();
    view.each([&](const auto entity, auto& chopping) {
        if (!reg.all_of<ActionBar>(entity)) {
            reg.emplace<ActionBar>(entity, chopping.start, chopping.start + duration);
        }

        if (time.current > chopping.start + duration) {
            auto& storage = reg.get<Storage>(entity);

            storage.add(Item(Item::wood, 10));

            // Destroy the tree entirely
            if (reg.valid(chopping.tree)) {
                auto& map = reg.ctx<MapManager>();
                auto tree_pos = reg.get<Position>(chopping.tree);

                dy::destroyObject(reg, chopping.tree);

                // Ensure tile's object reference is cleared
                auto* tile = map.getTile(tree_pos);
                if (tile) {
                    tile->object = entt::null;
                }
            }

            reg.remove<Chopping>(entity);
            reg.remove<ActionBar>(entity);
        }
    });
}

void ChopSys::finish() {

}
