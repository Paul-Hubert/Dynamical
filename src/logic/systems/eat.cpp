#include "system_list.h"

#include <logic/components/eat.h>
#include <logic/components/storage.h>
#include <logic/components/item.h>
#include <logic/components/basic_needs.h>

using namespace dy;

void EatSys::preinit() {
    
}

void EatSys::init() {
    
}

void EatSys::tick(float dt) {
    
    OPTICK_EVENT();
    
    auto& time = reg.ctx<Time>();
    
    auto view = reg.view<Eat>();
    view.each([&](const auto entity, auto& eat) {
        if(time.current > eat.start + 10 * Date::seconds_in_a_minute) {
            auto& needs = reg.get<BasicNeeds>(entity);
            float nutrition = 0.5;
            int amount = needs.hunger / nutrition;
            
            auto& storage = reg.get<Storage>(entity);
            int stack = storage.amount(eat.food);
            if(stack - amount < 0) {
                amount = stack;
            }
            storage.remove(Item(eat.food, amount));
            
            needs.hunger -= amount * nutrition;
            reg.remove<Eat>(entity);
        }
    });
    
}

void EatSys::finish() {
    
}
