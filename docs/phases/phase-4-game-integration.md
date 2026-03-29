# Phase 4: Game Integration

**Goal**: Wire LLMManager into the game loop, modify AISys to support dual-mode decision logic (LLM vs. score-based fallback), and handle conversation-based actions.

**Scope**: Core game loop integration point.

**Estimated Scope**: ~800 lines modified/new

**Completion Criteria**: Entities use LLM to make decisions, executing action queues with automatic prerequisite resolution. Fallback to score-based system when LLM is unavailable or disabled.

---

## Context: Why This Phase Matters

Phases 1-3 built the pieces in isolation. Phase 4 connects them:
- AISys now runs in dual modes (LLM-driven or score-based fallback)
- LLMManager is integrated into the game loop
- Action parameters are resolved and executed
- Conversations trigger new LLM requests

---

## 1. Modified AIC Component

### 1.1 Update AIC Header

Modify `src/ai/aic.h`:

```cpp
#pragma once
#include <memory>
#include <queue>
#include <entt/entt.hpp>

class Action;

struct AIC {
    // Existing
    std::unique_ptr<Action> action = nullptr;  // Current action being executed
    float score = 0.0f;                        // Score from behavior system
    bool interruptible = false;

    // NEW: LLM-driven action queue
    std::queue<std::unique_ptr<Action>> action_queue;

    // NEW: LLM control flags
    bool use_llm = true;                       // Enable LLM for this entity
    uint64_t pending_llm_request_id = 0;       // Current pending request ID (0 = none)
    bool waiting_for_llm = false;              // True if request in flight

    // Optional: track which personality seed this entity has (for cache key)
    uint32_t personality_seed = 0;
};
```

---

## 2. Modified AISys

### 2.1 AISys Header Update

Modify `src/ai/ai.h`:

```cpp
#pragma once
#include "../llm/llm_manager.h"
#include "../llm/prompt_builder.h"
#include "../llm/response_parser.h"
#include "../ai/actions/prerequisite_resolver.h"
#include <memory>

class System;  // Forward
class AISys : public System {
public:
    AISys();
    ~AISys() override;

    void tick(float dt) override;

    // Set the LLM manager (called from Game::start())
    void set_llm_manager(LLMManager* mgr) { llm_manager = mgr; }

private:
    LLMManager* llm_manager = nullptr;
    PromptBuilder prompt_builder;
    ResponseParser response_parser;
    PrerequisiteResolver prerequisite_resolver;

    // Helper methods
    void process_entities_with_llm(float dt);
    void process_entities_with_fallback(float dt);
    void poll_llm_results();
    void execute_action_queue(entt::entity entity, AIC& aic, float dt);
    void submit_llm_request(entt::entity entity);
    PromptContext build_prompt_context(entt::entity entity);
    void handle_conversation_messages(entt::entity entity);
};
```

### 2.2 AISys Implementation

Create `src/ai/ai.cpp` with dual-mode logic:

```cpp
#include "ai.h"
#include "aic.h"
#include "actions/action.h"
#include "actions/action_registry.h"
#include "personality/personality.h"
#include "speech/ai_memory.h"
#include "conversation/conversation_manager.h"
#include "../logic/components/basic_needs.h"
#include "../logic/components/position.h"
#include "../logic/components/object.h"
#include <entt/entt.hpp>

AISys::AISys() = default;
AISys::~AISys() = default;

void AISys::tick(float dt) {
    auto& reg = get_registry();

    // Poll LLM results from previous frame
    if (llm_manager) {
        poll_llm_results();
    }

    // Process each entity
    auto view = reg.view<AIC>();
    for (auto entity : view) {
        auto& aic = view.get<AIC>(entity);

        // Skip if currently executing action
        if (aic.action != nullptr && !aic.action->interruptible) {
            execute_action_queue(entity, aic, dt);
            continue;
        }

        // Entity is free to make a decision
        if (aic.action_queue.empty() && !aic.waiting_for_llm) {
            // Need a new decision

            if (aic.use_llm && llm_manager && llm_manager->is_available()) {
                // LLM mode
                submit_llm_request(entity);
            } else {
                // Fallback mode: use score-based behavior
                auto decisions = decide(entity);
                if (decisions != nullptr) {
                    aic.action = std::move(decisions);
                }
            }
        }

        // Execute current action or queue head
        if (!aic.action_queue.empty()) {
            execute_action_queue(entity, aic, dt);
        } else if (aic.action) {
            aic.action = aic.action->act(std::move(aic.action));
        }
    }
}

void AISys::poll_llm_results() {
    llm_manager->poll_results([this](const LLMResponse& response) {
        // Response callback — resolve pending requests
        auto& reg = get_registry();
        auto view = reg.view<AIC>();

        for (auto entity : view) {
            auto& aic = view.get<AIC>(entity);

            if (!aic.waiting_for_llm) continue;
            if (aic.pending_llm_request_id == 0) continue;

            // Check if this is the entity's response
            if (llm_manager->is_result_ready(aic.pending_llm_request_id)) {
                LLMResponse actual_response;
                if (llm_manager->try_get_result(aic.pending_llm_request_id, actual_response)) {
                    if (!actual_response.success) {
                        // Fallback to wandering on error
                        auto desc = ActionRegistry::instance().get(ActionID::Wander);
                        if (desc) {
                            aic.action_queue.push(
                                desc->create(reg, entity, ActionParams{})
                            );
                        }
                        aic.waiting_for_llm = false;
                        aic.pending_llm_request_id = 0;
                        continue;
                    }

                    // Parse response
                    auto decision = response_parser.parse(actual_response.parsed_json);

                    if (!decision.success) {
                        // Default to Wander on parse failure
                        auto desc = ActionRegistry::instance().get(ActionID::Wander);
                        if (desc) {
                            aic.action_queue.push(
                                desc->create(reg, entity, ActionParams{})
                            );
                        }
                        aic.waiting_for_llm = false;
                        aic.pending_llm_request_id = 0;
                        continue;
                    }

                    // Queue all parsed actions (with prerequisite resolution)
                    for (const auto& parsed : decision.actions) {
                        auto resolution = prerequisite_resolver.resolve(
                            reg, entity, parsed.action_id, parsed.params
                        );

                        if (resolution.satisfied && resolution.action_chain) {
                            aic.action_queue.push(std::move(resolution.action_chain));
                        }
                    }

                    // Mark memory as seen
                    if (auto* mem = reg.try_get<AIMemory>(entity)) {
                        mem->mark_all_seen();
                    }

                    aic.waiting_for_llm = false;
                    aic.pending_llm_request_id = 0;
                }
            }
        }
    });
}

void AISys::submit_llm_request(entt::entity entity) {
    auto& reg = get_registry();
    auto& aic = reg.get<AIC>(entity);

    // Build prompt
    auto ctx = build_prompt_context(entity);
    std::string prompt = prompt_builder.build_decision_prompt(ctx);
    std::string system_prompt = prompt_builder.build_system_prompt(ctx.personality);

    // Submit request
    uint64_t request_id = llm_manager->submit_request(prompt, system_prompt);

    aic.pending_llm_request_id = request_id;
    aic.waiting_for_llm = true;
}

PromptContext AISys::build_prompt_context(entt::entity entity) {
    auto& reg = get_registry();

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Entity#" + std::to_string(static_cast<uint32_t>(entity));

    // Get personality
    if (auto* pers = reg.try_get<Personality>(entity)) {
        ctx.personality = pers;
        ctx.entity_name = pers->archetype + " #" + std::to_string(static_cast<uint32_t>(entity));
    }

    // Get memory
    if (auto* mem = reg.try_get<AIMemory>(entity)) {
        ctx.memory = mem;
    }

    // Get needs
    if (auto* needs = reg.try_get<BasicNeeds>(entity)) {
        ctx.hunger = needs->hunger;
        ctx.energy = needs->energy;
    }

    // Get nearby entities (simplified)
    if (auto* pos = reg.try_get<Position>(entity)) {
        // Scan for nearby entities within some radius
        // Populate ctx.nearby_entities as JSON string
        ctx.nearby_entities = "[]";  // TODO: implement spatial query
    }

    // Get nearby resources
    ctx.nearby_resources = "[]";  // TODO: implement resource detection

    return ctx;
}

void AISys::execute_action_queue(entt::entity entity, AIC& aic, float dt) {
    auto& reg = get_registry();

    // Pop and execute head of queue
    if (!aic.action_queue.empty()) {
        auto& action = aic.action_queue.front();
        auto next = action->act(std::move(action));

        if (next != nullptr) {
            aic.action_queue.front() = std::move(next);
        } else {
            aic.action_queue.pop();
        }
    }
}

void AISys::handle_conversation_messages(entt::entity entity) {
    auto& reg = get_registry();

    // If entity has unread messages, generate a response
    if (auto* mem = reg.try_get<AIMemory>(entity)) {
        if (!mem->unread_messages.empty()) {
            // Trigger new LLM request (conversation response)
            // This happens in the next tick when action_queue is empty
            mem->clear_unread();  // Clear so we don't double-process
        }
    }
}

// Fallback: existing decide() logic (unchanged)
std::unique_ptr<Action> AISys::decide(entt::entity entity) {
    // ... existing behavior selection logic
    // Score behaviors (Wander, Eat, etc.)
    // Return highest-scoring action
    return nullptr;  // Placeholder
}
```

---

## 3. LLMManager Registration in Game

### 3.1 Modify Game::start()

In `src/logic/game.cpp`, integrate LLMManager:

```cpp
#include "game.h"
#include "../llm/llm_manager.h"
#include "../ai/ai.h"
#include "../ai/aic.h"

void Game::start() {
    // ... existing setup

    // Create and configure LLM manager
    auto llm_mgr = std::make_unique<LLMManager>(2);  // 2 worker threads
    llm_mgr->configure(
        settings.llm_provider,      // From config
        settings.llm_model,
        settings.llm_api_key
    );

    // Register with context (for access from systems)
    registry.ctx().emplace<LLMManager*>(llm_mgr.get());

    // Set LLM manager on AISys
    auto* ai_sys = systems.get_system<AISys>();
    if (ai_sys) {
        ai_sys->set_llm_manager(llm_mgr.get());
    }

    // Store manager in game
    this->llm_manager = std::move(llm_mgr);

    // ... rest of game initialization
}
```

Add to `src/logic/game.h`:

```cpp
class Game {
    // ... existing
private:
    std::unique_ptr<LLMManager> llm_manager;
};
```

---

## 4. Settings/Configuration

### 4.1 LLM Settings Schema

Modify `src/logic/settings.h`:

```cpp
struct Settings {
    // ... existing settings

    // LLM Configuration
    struct {
        std::string provider = "ollama";  // "ollama", "lm_studio", "claude", "openai"
        std::string model = "llama2";
        std::string api_key = "";

        bool enabled = true;
        float rate_limit_rps = 5.0f;
        bool batching_enabled = false;
        int batch_size = 3;
        bool caching_enabled = true;

        int worker_threads = 2;
    } llm;

    // ... serialization
};
```

### 4.2 Load/Save LLM Settings

Modify settings serialization (using Cereal):

```cpp
template<class Archive>
void serialize(Archive& ar, const Settings& s) {
    // ... existing
    ar(s.llm.provider, s.llm.model, s.llm.api_key, s.llm.enabled);
    ar(s.llm.rate_limit_rps, s.llm.batching_enabled, s.llm.batch_size);
    ar(s.llm.caching_enabled, s.llm.worker_threads);
}
```

---

## 5. Fallback Mode

When LLM is unavailable or disabled:

1. **LLMManager::is_available()** returns `false`
2. **AISys::tick()** detects this and calls existing `decide()` logic
3. Score-based behavior system continues unchanged
4. **No game crashes, no visible glitches** — seamless fallback

---

## 6. Conversation Integration

### 6.1 Conversation Manager in Game

Register ConversationManager with registry:

```cpp
// In Game::start()
auto* conv_mgr = std::make_unique<ConversationManager>();
registry.ctx().emplace<ConversationManager*>(conv_mgr.get());
```

### 6.2 Handle Trade/Talk Actions

When Trade or Talk action is executed:

```cpp
// In Trade/Talk action's act() method:
if (auto* conv_mgr = registry.ctx().find<ConversationManager*>()) {
    auto conv = conv_mgr->start_conversation(entity, target_entity);
    // ... add initial message
    // ... trigger LLM response on target_entity
}
```

---

## 7. CMakeLists.txt Updates

```cmake
# Link LLMManager into game
target_sources(dynamical PRIVATE
    # ... existing
    src/llm/llm_manager.h
    src/llm/llm_manager.cpp
    src/llm/prompt_builder.h
    src/llm/prompt_builder.cpp
    src/llm/response_parser.h
    src/llm/response_parser.cpp
    src/llm/decision_cache.h
    src/llm/decision_cache.cpp

    src/ai/aic.h                           # MODIFIED
    src/ai/ai.h / ai.cpp                   # MODIFIED
    src/ai/conversation/conversation.h
    src/ai/conversation/conversation_manager.h
    src/ai/conversation/conversation_manager.cpp
    src/logic/game.h / game.cpp             # MODIFIED
    src/logic/settings.h                    # MODIFIED
)
```

---

## 8. Testing Checklist

### Unit Tests

- [ ] AIC component initializes with empty action_queue
- [ ] ActionRegistry creates actions from parsed LLM responses
- [ ] PrerequisiteResolver chains actions correctly
- [ ] Action parameters are passed through and accessible in act()

### Integration Tests

- [ ] Game starts with LLMManager initialized
- [ ] Settings load/save LLM configuration
- [ ] AISys::tick() calls submit_llm_request() when queue is empty
- [ ] poll_llm_results() processes responses and populates action_queue
- [ ] Actions from queue execute in order
- [ ] Fallback to score-based behavior when LLM disabled
- [ ] Game runs at full speed with no blocking on LLM latency
- [ ] Entities continue wandering while waiting for LLM response

### Play-Test

- [ ] Launch game with Ollama running (or similar)
- [ ] Observe entities making varied decisions (hunger, proximity, personality)
- [ ] Stop Ollama, verify automatic fallback to wandering
- [ ] Verify personalities persist across save/load
- [ ] Observe action chains executing (e.g., Harvest → Eat)

---

## 9. Error Handling

From global doc (Section 18):

- **Problem 3: Entity Destroyed During Async Wait**
  - Solution: `reg.valid(result.entity)` in poll_llm_results() ✓

- **Problem 4: Conversations Between Destroyed Entities**
  - Solution: ConversationManager checks entity validity in tick() ✓

- **Problem 6: LLM Server Unavailable**
  - Solution: Health check via `is_available()`, fallback to decide() ✓

---

## 10. Performance Notes

From global doc (Section 17):

- **Action Queue**: Most impactful — LLM returns 3-5 actions, 32s before new request
- **Cache**: 40-70% hit rate (discretized state)
- **Rate Limiting**: Token bucket, configurable per UI
- **Fallback Actions**: Entities always have valid action (wander while waiting)

**Steady-state**: 50 entities, 200ms latency, 2 workers → **< 1 request in queue**

---

## 11. Dependencies

- **Depends on**: Phases 1, 2, 3 (all systems built)
- **Blocks**: Phase 5 (UI), Phase 6 (action implementations)
- **Integration point**: Core game loop in `src/logic/game.cpp`

---

## 12. Next Phase

**Phase 5** adds ImGui UI for visualizing entity state (speech bubbles, decision process) and configuring LLM settings at runtime.

---

**Reference Sections from Global Doc:**
- Section 3: Architecture Overview
- Section 3.3: Dual-Mode Decision Logic
- Section 15: Migration Path
- Problem/Solution sections: #3, #4, #6
