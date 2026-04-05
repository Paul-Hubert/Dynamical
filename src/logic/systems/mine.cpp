#include "system_list.h"

#include <logic/components/mining.h>
#include <logic/components/storage.h>
#include <logic/components/time.h>
#include <logic/components/item.h>
#include <logic/components/action_bar.h>

using namespace dy;

void MineSys::preinit() {

}

void MineSys::init() {

}

void MineSys::tick(double dt) {

    OPTICK_EVENT();

    constexpr time_t duration = 10 * Time::minute;

    auto& time = reg.ctx<Time>();

    auto view = reg.view<Mining>();
    view.each([&](const auto entity, auto& mining) {
        if (!reg.all_of<ActionBar>(entity)) {
            reg.emplace<ActionBar>(entity, mining.start, mining.start + duration);
        }

        if (time.current > mining.start + duration) {
            auto& storage = reg.get<Storage>(entity);

            storage.add(Item(Item::stone, 10));

            reg.remove<Mining>(entity);
            reg.remove<ActionBar>(entity);
        }
    });
}

void MineSys::finish() {

}
