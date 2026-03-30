#include "ai.h"

#include "util/log.h"

#include "aic.h"
#include "speech/speech_bubble.h"

#include "behaviors/eat_behavior.h"
#include "behaviors/wander_behavior.h"

#include <ai/action_registry.h>
#include <ai/action_id.h>
#include <ai/prerequisite_resolver.h>
#include <ai/memory/ai_memory.h>
#include <ai/personality/personality.h>

#include <logic/map/map_manager.h>

#include <logic/components/basic_needs.h>
#include <logic/components/position.h>
#include <logic/components/path.h>

using namespace dy;

AISys::AISys(entt::registry& reg) : System(reg) {

}

void AISys::tick(double dt) {

    OPTICK_EVENT();

    // Poll LLM results from previous frame(s)
    if (llm_manager) {
        poll_llm_results();
    }

    auto view = reg.view<AIC>();

    view.each([&](const auto entity, auto& ai) {

        if (!ai.action || ai.action->interruptible) {
            if (!ai.action_queue.empty()) {
                // Consume next action from LLM queue
                ai.action = std::move(ai.action_queue.front());
                ai.action_queue.pop();
            } else if (!ai.waiting_for_llm) {
                bool can_use_llm = llm_manager
                    && llm_manager->is_available()
                    && reg.all_of<Personality>(entity);

                if (can_use_llm) {
                    submit_llm_request(entity, ai);
                    decide(entity, ai);  // wander/eat while waiting
                } else {
                    decide(entity, ai);
                }
            }
        }

        // Capture describe/failure before act() potentially destroys the action
        std::string current_desc = ai.action ? ai.action->describe() : "";
        bool was_active = ai.action != nullptr;

        if (ai.action) {
            ai.action = ai.action->act(std::move(ai.action), dt);
        }

        // Action just completed (returned nullptr)
        if (was_active && !ai.action) {
            // Write to AIMemory component if present
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

void AISys::poll_llm_results() {
    auto view = reg.view<AIC>();
    for (auto entity : view) {
        if (!reg.valid(entity)) continue;
        auto& ai = view.get<AIC>(entity);
        if (!ai.waiting_for_llm || ai.pending_llm_request_id == 0) continue;

        LLMResponse response;
        if (llm_manager->try_get_result(ai.pending_llm_request_id, response)) {
            ai.waiting_for_llm = false;
            ai.pending_llm_request_id = 0;

            if (!response.success) continue;

            auto decision = response_parser.parse(response.parsed_json);
            if (!decision.success) continue;

            for (const auto& parsed : decision.actions) {
                auto action = PrerequisiteResolver::instance().resolve(
                    parsed.action_id, reg, entity, parsed.params
                );
                if (action) {
                    ai.action_queue.push(std::move(action));
                }
            }

            if (reg.all_of<AIMemory>(entity)) {
                reg.get<AIMemory>(entity).mark_all_seen();
            }

            if (!decision.thought.empty() || !decision.dialogue.empty()) {
                reg.emplace_or_replace<SpeechBubble>(entity, SpeechBubble{
                    .thought  = decision.thought,
                    .dialogue = decision.dialogue,
                });
            }
        }
    }
}

void AISys::submit_llm_request(entt::entity entity, AIC& ai) {
    std::string name = "Entity#" + std::to_string(static_cast<uint32_t>(entity));
    if (reg.all_of<Personality>(entity)) {
        auto& pers = reg.get<Personality>(entity);
        name = pers.archetype + " #" + std::to_string(static_cast<uint32_t>(entity));
    }

    PromptContext ctx = build_context(reg, entity, name);
    std::string prompt = prompt_builder.build_decision_prompt(ctx);
    std::string sys_prompt = prompt_builder.build_system_prompt(ctx.personality);

    uint64_t id = llm_manager->submit_request(prompt, sys_prompt);
    ai.pending_llm_request_id = id;
    ai.waiting_for_llm = true;
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
