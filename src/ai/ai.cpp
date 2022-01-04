#include "ai.h"

#include "util/log.h"

#include "aic.h"

#include "behaviors/eat_behavior.h"
#include "behaviors/wander_behavior.h"

#include <logic/map/map_manager.h>

#include <logic/components/basic_needs.h>
#include <logic/components/position.h>
#include <logic/components/path.h>

using namespace dy;

AISys::AISys(entt::registry& reg) : System(reg) {
    
}

void AISys::tick(float dt) {
    
    OPTICK_EVENT();
    
    auto view = reg.view<AIC>();
    
    view.each([&](const auto entity, auto& ai) {
        
        if(!ai.action || ai.action->interruptible) {
            decide(entity, ai);
        }
        
        ai.action = ai.action->act(std::move(ai.action));
        
    });
    
}

void AISys::decide(entt::entity entity, AIC& ai) {
    
    OPTICK_EVENT();
    
    float max_score = 0;
    std::unique_ptr<Behavior> max_behavior;
    
    testBehavior<WanderBehavior>(entity, max_score, max_behavior);

    if(reg.all_of<BasicNeeds>(entity)) {
        testBehavior<EatBehavior>(entity, max_score, max_behavior);
    }

    if(max_score > ai.score || ai.action == nullptr) {
        ai.score = max_score;
        ai.action = max_behavior->action();
    }
    
}

AISys::~AISys() {
    
}

