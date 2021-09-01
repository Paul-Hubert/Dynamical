#include "ai.h"

#include "util/log.h"

#include "aic.h"
#include "actions/wander_action.h"
#include "actions/eat_action.h"

#include <logic/map/map_manager.h>

#include <logic/components/basic_needs.h>
#include "logic/components/position.h"
#include <logic/components/path.h>

using namespace dy;

AISys::AISys(entt::registry& reg) : System(reg) {
    
}

void AISys::tick(float dt) {
    
    auto view = reg.view<AIC, const Position>();
    
    view.each([&](const auto entity, auto& ai, const auto position) {
        
        if(!ai.action || ai.action->interruptible) {
            decide(entity, ai);
        }
        
        ai.action->act(position);
        
    });
    
}

void AISys::decide(entt::entity entity, AIC& ai) {
    
    float max_score;
    std::unique_ptr<Action> max_action;
    
    testAction<WanderAction>(entity, max_score, max_action);
    
    if(reg.all_of<BasicNeeds>(entity)) {
        testAction<EatAction>(entity, max_score, max_action);
    }
    
    if(max_score > ai.score) {
        ai.action = std::move(max_action);
    }
    
}

AISys::~AISys() {
    
}

