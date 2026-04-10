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
#include <ai/identity/entity_identity.h>

#include <ai/conversation/conversation_manager.h>
#include <logic/map/map_manager.h>

#include <logic/components/basic_needs.h>
#include <logic/components/position.h>
#include <logic/components/path.h>

#include <random>

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

        // Conversation responders are managed by the initiator's TalkAction.
        // Don't submit LLM requests or assign new actions while they're passive.
        if (reg.all_of<ConversationResponder>(entity)) {
            if (ai.action) {
                ai.action = ai.action->act(std::move(ai.action), dt);
            }
            return;
        }

        // Identity gate — block decision prompt until identity is ready
        if (reg.all_of<EntityIdentity>(entity)) {
            auto& identity = reg.get<EntityIdentity>(entity);
            if (!identity.ready) {
                if (identity.pending_request_id == 0
                    && llm_manager && llm_manager->is_available()) {
                    submit_identity_request(entity, identity);
                }
                decide(entity, ai);  // wander/eat while waiting
                return;
            }
        }

        if (!ai.action || ai.action->interruptible) {
            if (!ai.action_queue.empty()) {
                // Consume next action from LLM queue
                ai.action = std::move(ai.action_queue.front());
                ai.action_queue.pop();
            } else if (!ai.waiting_for_llm && !ai.draining_llm_queue) {
                bool can_use_llm = llm_manager
                    && llm_manager->is_available()
                    && reg.all_of<EntityIdentity>(entity)
                    && reg.get<EntityIdentity>(entity).ready;

                if (can_use_llm) {
                    submit_llm_request(entity, ai);
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

        // Clear draining flag when queue is fully consumed and either no action remains or current action is interruptible
        if (ai.draining_llm_queue && ai.action_queue.empty() && (!ai.action || ai.action->interruptible)) {
            ai.draining_llm_queue = false;
        }

        // Capture failure_reason on same tick it's set (before next act() call)
        if (ai.action && !ai.action->failure_reason.empty()) {
            ai.last_failure_reason = ai.action->failure_reason;
        }

    });

}

void AISys::poll_identity_results() {
    auto view = reg.view<EntityIdentity>();
    for (auto entity : view) {
        if (!reg.valid(entity)) continue;
        auto& identity = view.get<EntityIdentity>(entity);
        if (identity.ready || identity.pending_request_id == 0) continue;

        LLMResponse response;
        if (!llm_manager->try_get_result(identity.pending_request_id, response)) continue;

        identity.pending_request_id = 0;

        if (!response.success) {
            dy::log(dy::Level::warning) << "[AI Identity] Generation failed for entity "
                                     << static_cast<uint32_t>(entity)
                                     << " — will retry next tick\n";
            continue;  // pending_request_id=0 triggers retry next tick
        }

        auto& j = response.parsed_json;
        if (j.is_object()) {
            identity.name                    = j.value("name", "Unknown");
            identity.personality_type        = j.value("personality_type", "wanderer");
            identity.personality_description = j.value("personality_description", "");
        } else {
            identity.name             = "Unknown";
            identity.personality_type = "wanderer";
        }
        identity.ready = true;

        dy::log(dy::Level::info) << "[AI Identity] Entity "
                                 << static_cast<uint32_t>(entity)
                                 << " is now \"" << identity.name
                                 << "\" (" << identity.personality_type << ")\n";
    }
}

void AISys::submit_identity_request(entt::entity entity, EntityIdentity& identity) {
    const std::string prompt =
        "Generate a unique identity for a human character in a survival world.\n"
        "Return a JSON object with exactly these fields:\n"
        "{\n"
        "  \"name\": \"<single first name, e.g. Mira, Torven, Selja>\",\n"
        "  \"personality_type\": \"<one-word qualifier, e.g. trader, hermit, socialite, warrior, scholar, wanderer, craftsman>\",\n"
        "  \"personality_description\": \"<one vivid sentence describing their core personality, quirks, and motivations>\"\n"
        "}\n"
        "Be creative and vary names and archetypes. Return only the JSON object, no extra text.";

    const std::string system_prompt =
        "You are a creative fiction writer generating brief character identities for a medieval survival simulation. "
        "Respond only with a valid JSON object. No markdown fences, no explanation.";

    // High temperature + random seed maximises name/personality variety across entities
    static std::mt19937 rng{std::random_device{}()};
    int seed = static_cast<int>(rng() & 0x7FFFFFFFu);  // >= 0, satisfies provider seed >= -1
    identity.pending_request_id = llm_manager->submit_request(prompt, system_prompt, 1.0f, seed);

    dy::log(dy::Level::debug) << "[AI Identity] Submitted identity request "
                              << identity.pending_request_id << " for entity "
                              << static_cast<uint32_t>(entity) << "\n";
}

void AISys::poll_llm_results() {
    // Identity results must be polled first
    poll_identity_results();

    auto view = reg.view<AIC>();
    for (auto entity : view) {
        if (!reg.valid(entity)) continue;
        auto& ai = view.get<AIC>(entity);
        if (!ai.waiting_for_llm || ai.pending_llm_request_id == 0) continue;

        LLMResponse response;
        if (llm_manager->try_get_result(ai.pending_llm_request_id, response)) {
            uint64_t request_id = ai.pending_llm_request_id;
            std::string name = "Entity#" + std::to_string(static_cast<uint32_t>(entity));
            if (reg.all_of<EntityIdentity>(entity)) {
                const auto& eid = reg.get<EntityIdentity>(entity);
                if (eid.ready) name = eid.name;
            }

            dy::log(dy::Level::debug) << "[AI Entity] " << name << " received LLM response for request ID " << request_id
                                      << " (Success: " << (response.success ? "true" : "false") << ")\n";

            ai.waiting_for_llm = false;
            ai.pending_llm_request_id = 0;

            if (!response.success) continue;

            auto decision = response_parser.parse(response.parsed_json);
            if (!decision.success) continue;

            for (const auto& parsed : decision.actions) {
                auto chain = PrerequisiteResolver::instance().resolve(
                    parsed.action_id, reg, entity, parsed.params
                );
                for (auto& action : chain) {
                    ai.action_queue.push(std::move(action));
                }
            }

            if (!ai.action_queue.empty()) {
                ai.draining_llm_queue = true;
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
    PromptContext ctx = build_context(reg, entity);
    std::string prompt = prompt_builder.build_decision_prompt(ctx);
    std::string sys_prompt = prompt_builder.build_system_prompt(
        ctx.personality_type, ctx.personality_description);

    uint64_t id = llm_manager->submit_request(prompt, sys_prompt);
    ai.pending_llm_request_id = id;
    ai.waiting_for_llm = true;

    dy::log(dy::Level::debug) << "[AI Entity] " << ctx.entity_name
                              << " submitted LLM request ID " << id << "\n";
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
        ActionParams params;
        auto chain = PrerequisiteResolver::instance().resolve(action_id, reg, entity, params);
        if (!chain.empty()) {
            ai.action = std::move(chain.front());
            for (size_t i = 1; i < chain.size(); ++i) {
                ai.action_queue.push(std::move(chain[i]));
            }
        }
    }

}

AISys::~AISys() {

}
