#include "system_list.h"

#include <logic/components/eat.h>
#include <logic/components/storage.h>
#include <logic/components/item.h>
#include <logic/components/basic_needs.h>
#include <logic/components/action_bar.h>

using namespace dy;

void EatSys::preinit() {
    
}

void EatSys::init() {
    
}

void EatSys::tick(double dt) {
    
    OPTICK_EVENT();
    
    constexpr time_t duration = 10 * Time::minute;
    
    auto& time = reg.ctx<Time>();
    
    auto view = reg.view<Eat>();
    view.each([&](const auto entity, auto& eat) {
        if(!reg.all_of<ActionBar>(entity)) {
            reg.emplace<ActionBar>(entity, eat.start, eat.start + duration);
        }
        
        if(time.current > eat.start + duration) {
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
            reg.remove<ActionBar>(entity);
        }
    });
    
}

void EatSys::finish() {
    
}
