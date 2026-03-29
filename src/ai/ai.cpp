#include "ai.h"

#include "util/log.h"

#include "aic.h"

#include "behaviors/eat_behavior.h"
#include "behaviors/wander_behavior.h"

#include <ai/action_registry.h>
#include <ai/action_id.h>
#include <ai/prerequisite_resolver.h>
#include <ai/memory/ai_memory.h>

#include <logic/map/map_manager.h>

#include <logic/components/basic_needs.h>
#include <logic/components/position.h>
#include <logic/components/path.h>

using namespace dy;

AISys::AISys(entt::registry& reg) : System(reg) {
    
}

void AISys::tick(double dt) {

    OPTICK_EVENT();

    auto view = reg.view<AIC>();

    view.each([&](const auto entity, auto& ai) {

        if(!ai.action || ai.action->interruptible) {
            decide(entity, ai);
        }

        // Capture describe/failure before act() potentially destroys the action
        std::string current_desc = ai.action ? ai.action->describe() : "";
        bool was_active = ai.action != nullptr;

        ai.action = ai.action->act(std::move(ai.action), dt);

        // Action just completed (returned nullptr)
        if (was_active && !ai.action) {
            // Phase 2+: write to AIMemory component if present
            if (reg.all_of<AIMemory>(entity)) {
                auto& mem = reg.get<AIMemory>(entity);
                if (!ai.last_failure_reason.empty()) {
                    mem.add_event("failed_action", ai.last_failure_reason);
                } else if (!current_desc.empty()) {
                    mem.add_event("completed_action", current_desc);
                }
            }
            ai.last_failure_reason.clear();
        }

        // Capture failure_reason on same tick it's set (before next act() call)
        if (ai.action && !ai.action->failure_reason.empty()) {
            ai.last_failure_reason = ai.action->failure_reason;
        }

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

        // Resolve prerequisites and create action chain
        ActionID action_id = max_behavior->get_action_id();
        ActionParams params;  // Empty for now; will be populated in Phase 2+ for LLM
        ai.action = PrerequisiteResolver::instance().resolve(action_id, reg, entity, params);
    }

}

AISys::~AISys() {
    
}

