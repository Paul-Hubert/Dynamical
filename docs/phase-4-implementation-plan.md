# Phase 4 Implementation Plan: Game Loop Integration

## Overview

Phase 4 integrates Phase 3's LLM infrastructure into the main game loop, enabling AI entities to make LLM-driven decisions during gameplay.

**Status**: Planning phase
**Dependencies**: Phase 3 ✅ (Complete), Phase 1 ✅, Phase 2 ✅
**Estimated Scope**: ~1000 lines of modifications across AISys, Game, and action selection

---

## Architecture

### Integration Points

```
Game Loop (src/logic/game.h)
    │
    ├─► LLMManager (stored as member)
    │
    └─► AISys::tick(dt)
            │
            ├─► For each entity:
            │   ├─► Pre-tick (existing behavior selection)
            │   │   └─► IF needs LLM decision:
            │   │       └─► PromptBuilder → submit_request()
            │   │
            │   ├─► Main tick (action execution)
            │   │   └─► Poll LLMManager for results
            │   │
            │   └─► Post-tick (memory/conversation updates)
            │       └─► Mark memory as seen (Phase 2 integration)
            │
            └─► Fallback behavior (if LLM unavailable/failed)
```

---

## Implementation Steps

### Step 1: Store LLMManager in Game Class

**File**: `src/logic/game.h`

```cpp
class Game {
    // ... existing members ...
    std::unique_ptr<LLMManager> llm_manager;

public:
    LLMManager& get_llm_manager() { return *llm_manager; }
};
```

**Initialization** (in `Game::start()`):
```cpp
llm_manager = std::make_unique<LLMManager>(4);  // 4 worker threads

// Read from config or environment
std::string provider = config.get("llm.provider", "ollama");
std::string model = config.get("llm.model", "llama2");
std::string api_key = std::getenv("ANTHROPIC_API_KEY") ?: "";

llm_manager->configure(provider, model, api_key);
llm_manager->set_rate_limit(config.get("llm.rate_limit", 5));
llm_manager->set_cache_enabled(config.get("llm.cache_enabled", true));
```

**Shutdown** (in `Game::~Game()` or `Game::stop()`):
```cpp
llm_manager->shutdown();  // Wait for worker threads gracefully
```

---

### Step 2: Extend AIC Component (Behavior Selection Tracking)

**File**: `src/ai/aic.h`

```cpp
struct AIC {
    // ... existing fields ...

    // Phase 4: Track decision-making
    uint64_t pending_llm_request_id = 0;  // Request ID from LLMManager
    bool waiting_for_llm_decision = false; // Currently awaiting LLM response
    std::chrono::steady_clock::time_point llm_request_time;

    // Phase 4: Fallback strategy
    enum class DecisionMode {
        Behavior,     // Use behavior-driven actions
        LLM,          // Use LLM decisions
        Cached        // Use cached LLM decision
    };
    DecisionMode decision_mode = DecisionMode::Behavior;
};
```

---

### Step 3: Modify AISys::tick() for LLM Integration

**File**: `src/ai/systems/aisys.cpp` (or wherever AISys is implemented)

**New decision flow**:

```cpp
void AISys::tick(float dt, entt::registry& reg, Game& game) {
    auto llm_mgr = &game.get_llm_manager();

    // Pre-tick: Submit LLM requests for entities that need decisions
    for (auto [entity, aic, personality] : reg.view<AIC, Personality>().each()) {
        if (should_request_llm_decision(aic, personality, dt)) {
            // Build prompt context
            PromptContext ctx = build_context(reg, entity, get_entity_name(entity));

            // Build full prompt
            PromptBuilder builder;
            std::string prompt = builder.build_decision_prompt(ctx);
            std::string system_prompt = builder.build_system_prompt(&personality);

            // Submit async request
            uint64_t request_id = llm_mgr->submit_request(prompt, system_prompt);

            // Store request info in AIC
            aic.pending_llm_request_id = request_id;
            aic.waiting_for_llm_decision = true;
            aic.llm_request_time = std::chrono::steady_clock::now();
            aic.decision_mode = AIC::DecisionMode::LLM;
        }
    }

    // Mid-tick: Poll for LLM results
    llm_mgr->poll_results([&](const LLMResponse& resp) {
        // Find entity waiting for this result
        auto entity = find_entity_by_request_id(reg, resp.request_id);
        if (entity == entt::null) return;

        auto& aic = reg.get<AIC>(entity);

        if (resp.success) {
            // Parse LLM response into actions
            ResponseParser parser;
            auto decision = parser.parse(resp.parsed_json);

            if (decision.success && !decision.actions.empty()) {
                // Execute first LLM-generated action
                enqueue_llm_decision(entity, decision, game);
            } else {
                // LLM returned invalid response, fallback to behavior
                aic.decision_mode = AIC::DecisionMode::Behavior;
            }
        } else {
            // LLM call failed, fallback to behavior
            aic.decision_mode = AIC::DecisionMode::Behavior;
        }

        // Clear pending request
        aic.waiting_for_llm_decision = false;
        aic.pending_llm_request_id = 0;
    });

    // Main tick: Execute current actions (existing logic)
    for (auto [entity, aic] : reg.view<AIC>().each()) {
        if (aic.current_action) {
            aic.current_action->tick(dt);

            if (aic.current_action->is_complete()) {
                // Action finished, action will be destroyed
                aic.last_action_description = aic.current_action->describe();
                aic.current_action = nullptr;
            }
        } else if (!aic.waiting_for_llm_decision) {
            // Need to choose next action
            if (aic.decision_mode == AIC::DecisionMode::Behavior) {
                select_behavior_action(entity, reg, game);
            }
            // If LLM, action already queued by llm_mgr poll above
        }
    }

    // Post-tick: Mark memories as seen
    for (auto [entity, memory] : reg.view<AIMemory>().each()) {
        if (was_used_in_llm_prompt(entity, reg)) {
            memory.mark_all_seen();  // Phase 2 integration
        }
    }
}
```

---

### Step 4: Decision Timing Strategy

**Key Question**: Should every entity request LLM decision every frame?

**Answer**: No. Implement per-entity decision timing to reduce LLM load.

**Strategy**:
```cpp
bool should_request_llm_decision(const AIC& aic, const Personality& personality, float dt) {
    // Already waiting for a decision
    if (aic.waiting_for_llm_decision) return false;

    // No action currently running
    if (aic.current_action != nullptr) return false;

    // Decision interval based on personality (more deliberative personalities decide less often)
    float decision_interval = personality.traits["deliberativeness"].value * 2.0f;  // 0-2 seconds

    // Check time since last decision
    float time_since_decision = ...;  // Track in AIC
    if (time_since_decision < decision_interval) return false;

    return true;  // Time to request new decision
}
```

**Decision Interval Examples**:
- Impulsive entity (deliberativeness 0.2): Decides every 0.4 seconds
- Thoughtful entity (deliberativeness 0.9): Decides every 1.8 seconds

---

### Step 5: Fallback Behavior Handling

**Fallback Triggers**:
1. LLM provider unavailable (`llm_mgr->is_available() == false`)
2. Request timeout (> 10 seconds waiting)
3. LLM returned invalid response
4. ResponseParser failed to extract valid actions

**Fallback Strategy**:
```cpp
void handle_llm_fallback(entt::entity entity, entt::registry& reg, Game& game) {
    auto& aic = reg.get<AIC>(entity);

    // Mark fallback in decision mode
    aic.decision_mode = AIC::DecisionMode::Behavior;

    // Select behavior-driven action instead
    select_behavior_action(entity, reg, game);

    // Optionally log for debugging
    if (game.config.debug.log_ai_decisions) {
        std::cout << entity << ": LLM fallback, using behavior" << std::endl;
    }
}
```

---

### Step 6: Queue Prioritization (Optional Enhancement)

For games with many entities competing for LLM decisions:

```cpp
struct PrioritizedRequest {
    uint64_t request_id;
    entt::entity entity;
    int priority;  // 0-10, higher = more important
    float submission_time;
};

// Instead of simple queue, use priority queue
std::priority_queue<PrioritizedRequest> high_priority_queue;

// Assign priority based on game state
int calculate_priority(entt::entity entity, entt::registry& reg) {
    int priority = 5;  // Default

    // Prioritize entities in combat
    if (reg.all_of<InCombat>(entity)) priority += 3;

    // Prioritize player-controlled entities
    if (is_player_entity(entity)) priority += 2;

    // Deprioritize distant entities
    if (is_far_from_camera(entity)) priority -= 1;

    return priority;
}
```

---

### Step 7: Integration with PrerequisiteResolver

**File**: `src/ai/prerequisite_resolver.h`

No changes needed! Phase 3 ResponseParser already validates ActionID against enum.

PrerequisiteResolver simply receives valid ActionID + ActionParams:

```cpp
// In Phase 4, after LLM decision:
ResponseParser parser;
auto decision = parser.parse(llm_response.parsed_json);

for (const auto& parsed_action : decision.actions) {
    // Create action through registry
    auto action = ActionRegistry::instance().create_action(
        parsed_action.action_id,
        parsed_action.params,
        entity,
        reg
    );

    // Verify prerequisites
    if (prerequisite_resolver.can_execute(action, entity, reg)) {
        aic.current_action = action;
        break;  // First valid action
    } else {
        // Action blocked by prerequisites, try next
    }
}
```

---

### Step 8: Memory Marking (Phase 2 Integration)

**File**: `src/ai/memory/ai_memory.h` - already has `mark_all_seen()` method

**Usage in Phase 4**:

```cpp
// After including entity's AIMemory in LLM prompt:
if (reg.all_of<AIMemory>(entity)) {
    auto& memory = reg.get<AIMemory>(entity);
    memory.mark_all_seen();  // Prevents re-processing same events
    memory.clear_unread();   // Clear unread messages
}
```

**Design Benefit**: Next LLM decision will only see NEW events, reducing prompt length and cost.

---

### Step 9: Conversation Manager Integration

**File**: `src/ai/conversation/conversation_manager.h` - accessed via `game.conversation_manager`

**Usage in Phase 4**:

```cpp
// In PromptBuilder::build_decision_prompt():
auto& cm = game.conversation_manager;
auto active_convs = cm.get_active_for(entity);

if (!active_convs.empty()) {
    prompt += "\n=== ACTIVE CONVERSATIONS ===\n";
    for (const auto& conv : active_convs) {
        prompt += format_conversation_snippet(conv);
    }
}
```

**Enables**: Entities make decisions based on ongoing conversations with other entities.

---

## Configuration & Tuning

### config.json Additions

```json
{
  "llm": {
    "enabled": true,
    "provider": "ollama",
    "model": "llama2",
    "api_key": "${ANTHROPIC_API_KEY}",
    "rate_limit": 5,
    "cache_enabled": true,
    "batch_size": 3,
    "batching_enabled": false,
    "worker_threads": 4,
    "request_timeout_seconds": 10,
    "fallback_to_behavior": true,
    "debug": {
      "log_decisions": true,
      "log_prompts": false,
      "log_responses": false
    }
  }
}
```

### Tuning Knobs

| Parameter | Impact | Default | Range |
|---|---|---|---|
| `rate_limit` | Max requests/sec | 5 | 1-100 |
| `worker_threads` | Concurrency | 4 | 1-16 |
| `decision_interval` | How often to ask LLM | 0.5-2.0s | 0.1-10s |
| `request_timeout` | Max wait for response | 10s | 5-30s |
| `cache_enabled` | Reuse decisions | true | true/false |

---

## Testing Strategy

### Unit Tests (Phase 4 specific)
- AISys::tick() with mocked LLMManager
- Fallback behavior when LLM unavailable
- Priority queue ordering
- Memory marking after LLM decision
- Conversation manager integration

### Integration Tests
- Full game loop with real LLMManager
- Multiple entities requesting LLM decisions simultaneously
- Provider switching (Ollama → Claude)
- Performance profiling (FPS impact)

### Gameplay Tests
- Verify entities make personality-aligned decisions
- Verify entities react to conversation context
- Verify memory prevents repetitive actions
- Verify fallback behavior when LLM fails

---

## Deployment Considerations

### Local Development
```bash
# Start Ollama (free local provider)
ollama run llama2

# Run game with Ollama
./dynamical --llm-provider=ollama --llm-model=llama2
```

### Distributed/Cloud
```bash
# Use cloud provider via API key
export ANTHROPIC_API_KEY="sk-..."
./dynamical --llm-provider=claude --llm-model=claude-3-sonnet
```

### Performance Budgeting
- **Per entity**: ~1-2 seconds for LLM decision + 0.1 ms for cached decision
- **For 100 entities with 1 decision every 2 seconds**:
  - ~50 decisions/sec at peak
  - ~25 decisions/sec average (rate-limited to 5 RPS)
  - Total latency: Spread across worker thread pool

---

## Migration Path from Phase 3 to Phase 4

### Phase 3 (Standalone Testing)
- ✅ LLMManager works independently
- ✅ Tests use mock providers
- ✅ No dependency on AISys or game loop

### Phase 4a (Behavior Integration)
- Link LLMManager into Game
- Create AISys::poll_llm_results()
- Entities still use behavior-driven decisions

### Phase 4b (LLM Integration)
- Implement LLM request submission in AISys
- Choose which entities use LLM vs behavior
- Add fallback logic

### Phase 4c (Full Integration)
- All configured entities use LLM
- Memory marking, conversation context
- Priority queue, timeout handling

---

## Known Issues & Workarounds

### Issue 1: LLM Latency Impacts Real-Time Feel
**Solution**: Queue multiple requests from multiple entities, use faster models for real-time decisions, fallback to behavior immediately if no response.

### Issue 2: LLM Hallucination (Invalid Actions)
**Solution**: ResponseParser validates against ActionID enum, falls back to behavior if invalid.

### Issue 3: Prompt Context Explosion
**Solution**: Limit memory events to recent N (e.g., 10 events), summarize when over limit.

### Issue 4: Cache Stale Decisions
**Solution**: Include time-based invalidation, respect significant state changes (hunger drop > 3).

---

## Success Criteria (Phase 4 Completion)

- [ ] LLMManager successfully integrated into Game::start()
- [ ] AISys::tick() submits and polls LLM requests
- [ ] Entities make LLM-driven decisions
- [ ] Fallback behavior works when LLM unavailable
- [ ] Performance acceptable (60 FPS maintained)
- [ ] Memory properly marked as seen
- [ ] Tests pass (unit + integration + gameplay)
- [ ] Configuration via config.json works
- [ ] Documentation complete

---

## Future Phases (Phase 5+)

### Phase 5: NPC Personality Variation
- Different personality archetypes make distinctly different decisions
- Measure decision uniqueness
- Add personality metrics to simulation results

### Phase 6: Dynamic Learning
- Collect outcomes of LLM decisions
- Fine-tune models on game-specific data
- A/B test different models

### Phase 7: Multi-Entity Coordination
- Entities negotiate via LLM
- Coalition formation
- Emergent group behaviors

---

## References

- **Phase 3 Implementation**: `docs/phase-3-implementation.md`
- **Phase 1 (Action System)**: `docs/phase-1-action-system.md`
- **Phase 2 (Personality, Memory)**: `docs/phase-2-personality-memory.md`
- **Architecture Overview**: `docs/architecture.md`

