# LLM-AI System: Implementation Phases

This directory contains consolidated documentation for each phase of implementing the LLM-driven AI system in Dynamical.

**Read these instead of the global `llm-ai-system.md` when implementing specific phases.**

---

## Quick Navigation

| Phase | Title | Scope | Estimated LOC | Status |
|-------|-------|-------|---------------|--------|
| [1](./phase-1-action-system-foundation.md) | Action System Foundation | Registry, factory, parameters, stage machine | ~1500 | ✅ Complete |
| [2](./phase-2-personality-and-memory.md) | Personality & Memory | Data structures | ~800 | 🔲 Next |
| [3](./phase-3-llm-infrastructure.md) | LLM Infrastructure | ai-sdk-cpp integration, async queue | ~2000 | 🔲 |
| [4](./phase-4-game-integration.md) | Game Integration | AISys modifications, dual-mode logic | ~800 | 🔲 |
| [5](./phase-5-ui-and-visualization.md) | UI & Visualization | ImGui overlays, config panels | ~1200 | 🔲 |
| [6](./phase-6-action-implementation.md) | Action Implementation | 13 action state machines | ~3000 | 🔲 |

**Total**: ~9300 lines of new/modified code over **10-14 weeks**

---

## Implementation Order

Phases **must** be done sequentially:

```
Phase 1 → Phase 2 → Phase 3 → Phase 4 → Phase 5 → Phase 6
   ↓        ↓        ↓        ↓        ↓        ↓
Foundation Data   LLM      Integration Observ-  Game
           Struct  Engine             ability  Content
```

- **Phases 1-2 can overlap** (independent data structures)
- **Phase 3 testable standalone** (before Phase 4 integration)
- **Phase 5 can be deferred** (visualization, not required for gameplay)

---

## Phase Summaries

### Phase 1: Action System Foundation ✅
**What**: Action registry, factory pattern, parameter system, prerequisite resolver, **ActionStageMachine**, `describe()`, `failure_reason`, `dt` in action loop.

**Why**: Enable dynamic action creation from LLM responses. Named stages needed for speech bubbles and AIMemory.

**Key Files**: `action_id.h`, `action_registry.h`, `action_base.h`, `prerequisite_resolver.h`, **`action_stage.h`**

**Complete** — `HarvestAction` and `EatAction` refactored as reference implementations.

[Read Phase 1 →](./phase-1-action-system-foundation.md)

---

### Phase 2: Personality & Memory
**What**: Create Personality component (traits, archetype, motivation), AIMemory (events, unread messages), ConversationManager.

**Why**: Give each entity unique identity; track history for LLM context; enable two-way conversations.

**Key Files**: `personality.h`, `personality_generator.h`, `ai_memory.h`, `conversation_manager.h`

**Purely data structures** — no game logic yet.

[Read Phase 2 →](./phase-2-personality-and-memory.md)

---

### Phase 3: LLM Infrastructure
**What**: Integrate ai-sdk-cpp library; build LLMManager with worker threads, caching, and rate limiting; create PromptBuilder and ResponseParser.

**Why**: Connect to actual LLM (Ollama, LM Studio, Claude, OpenAI); handle async requests without blocking game.

**Key Files**: `llm_client.h`, `llm_manager.h`, `prompt_builder.h`, `response_parser.h`, `decision_cache.h`

**Testable independently** — can verify LLM pipeline before game integration.

[Read Phase 3 →](./phase-3-llm-infrastructure.md)

---

### Phase 4: Game Integration
**What**: Modify AISys to run in dual-mode (LLM vs. fallback); integrate LLMManager into game loop; handle action queues and prerequisite resolution.

**Why**: Make the LLM actually drive entity behavior in the game.

**Key Files**: `ai.h/cpp`, `aic.h` (modified), `game.h/cpp` (modified)

**This is where it all comes together.**

[Read Phase 4 →](./phase-4-game-integration.md)

---

### Phase 5: UI & Visualization
**What**: Add ImGui panels for entity state, LLM configuration, personality inspection, and speech bubbles.

**Why**: Observability — understand what the LLM decided and why. Configure providers at runtime.

**Key Files**: `speech_bubble_sys.h`, `llm_config_panel.h`, `personality_inspector.h`

**Optional but recommended** — helpful for debugging.

[Read Phase 5 →](./phase-5-ui-and-visualization.md)

---

### Phase 6: Action Implementation
**What**: Fill in the 13 action skeletons (Mine, Hunt, Build, Sleep, Trade, Talk, Craft, Fish, Explore, Flee) with full state machines and ECS systems.

**Why**: Actual gameplay — entities have rich, varied behaviors.

**Key Files**: `mine_action.h`, `hunt_action.h`, `trade_action.h`, etc. + corresponding ECS systems

**Longest phase** — can be parallelized if necessary.

[Read Phase 6 →](./phase-6-action-implementation.md)

---

## Testing Strategy

### Per-Phase Testing

- **Phase 1**: ActionRegistry populates, actions create with parameters, prerequisites chain
- **Phase 2**: Personalities generate reproducibly, serialization works, conversations track correctly
- **Phase 3**: LLM requests succeed for all providers, async queue works, responses parse into actions
- **Phase 4**: Entities use LLM decisions, fallback works when LLM unavailable, no blocking
- **Phase 5**: UI renders without crashes, config changes apply immediately
- **Phase 6**: Each action reaches completion, records in AIMemory, chains correctly

### Integration Test (End-to-End)

```
1. Launch game with Ollama running
2. Observe entity with low hunger
3. Verify LLM requests decision (via SpeechBubbleSys)
4. Confirm LLM response chains Harvest → Eat
5. Watch entity harvest then eat
6. Check AIMemory for recorded events
7. Stop Ollama
8. Verify fallback to score-based system
9. Entities continue wandering (no crash)
```

---

## File Structure Created

```
docs/phases/
├── README.md                          (this file)
├── phase-1-action-system-foundation.md
├── phase-2-personality-and-memory.md
├── phase-3-llm-infrastructure.md
├── phase-4-game-integration.md
├── phase-5-ui-and-visualization.md
└── phase-6-action-implementation.md
```

Each file is **self-contained** — contains all relevant sections from the global `llm-ai-system.md`, consolidated for that phase.

---

## Key Design Decisions

### ai-sdk-cpp as Sole HTTP Client
- **Why**: Single unified API for all LLM providers (Ollama, LM Studio, Claude, OpenAI)
- **How**: `LLMClient` wraps ai-sdk-cpp; all LLM calls route through it
- **Benefit**: Switch providers by editing config, no code changes

### Async + Non-Blocking
- **Why**: LLM latency (200ms-2000ms) would freeze game at 60 FPS
- **How**: Worker threads handle requests; results polled per-frame
- **Benefit**: Entities continue tasks while waiting for LLM decision

### Dual-Mode Fallback
- **Why**: Game must work even if LLM unavailable
- **How**: AISys checks `llm_manager->is_available()`, falls back to score-based `decide()`
- **Benefit**: No crashes, no error messages, seamless degradation

### Personality in Every Prompt
- **Why**: Same state + different personalities should yield different decisions
- **How**: Personality serialized into system prompt every request
- **Benefit**: Entities have consistent, unique personalities

### Infinite Inventory (via AIMemory)
- **Why**: Simplify resource system; focus on decision-making
- **How**: Resources tracked narratively in AIMemory events, not physical Storage
- **Benefit**: Less bookkeeping, actions focus on behavior not logistics

---

## Dependency Graph

```
           [Phase 1]
        Action System
             ↓
      ┌──────┴──────┐
      ↓             ↓
  [Phase 2]    [Phase 3]
Personality   LLM Stack
  & Memory    (parallel)
      ↓             ↓
      └──────┬──────┘
             ↓
        [Phase 4]
      Game Integration
             ↓
        ┌────┴────┐
        ↓         ↓
    [Phase 5]  [Phase 6]
   UI (opt)   Action Impl
        ↓         ↓
        └────┬────┘
             ↓
        Playable Game
```

---

## Quick Reference: What Each Phase Produces

| Phase | Produces | Consumes |
|-------|----------|----------|
| 1 ✅ | ActionRegistry, ActionBase, PrerequisiteResolver, **ActionStageMachine**, `describe()`, `failure_reason` | — |
| 2 | Personality component, AIMemory, ConversationManager | — |
| 3 | LLMClient, LLMManager, PromptBuilder, ResponseParser | Phase 1 (ActionID), Phase 2 (Personality, AIMemory) |
| 4 | Modified AISys, dual-mode logic | Phases 1-3 |
| 5 | SpeechBubbleSys, LLMConfigPanel, PersonalityInspector | Phases 1-4 |
| 6 | 13 action implementations, corresponding ECS systems | Phases 1-5 |

---

## Common Questions

### Q: Can I skip a phase?
**A**: No. Phases build sequentially. Phase 4 requires Phases 1-3.

### Q: Can I work on multiple phases in parallel?
**A**: Yes, Phases 1-2 are independent (both are data structures). Phase 3 can start once Phase 1 is done.

### Q: Should I implement all 13 actions in Phase 6?
**A**: Not necessarily. Implement core ones (Wander, Eat, Harvest, Sleep) first. Advanced ones (Trade, Talk, Build) can come later.

### Q: How do I test Phase 3 without Phase 4?
**A**: Create a standalone test that submits a prompt to LLMManager and waits for result. See Phase 3 "Testing Checklist" section.

### Q: What if the LLM is slow or unavailable?
**A**: The game falls back to score-based behavior automatically (Phase 4 Fallback Mode). No crashes.

### Q: Can I change providers at runtime?
**A**: Yes! Phase 5 LLM config panel lets you switch providers/models in the UI.

---

## Debugging Tips

### Phase-Specific Troubleshooting

**Phase 1**: ActionRegistry not populating?
- Check that `ActionBase::registered` static initializer is called
- Verify action's `.cpp` file has registration line

**Phase 2**: Personalities not saving?
- Check Cereal serialization methods (`serialize()`) are defined
- Verify `cereal` is linked in CMakeLists

**Phase 3**: LLM requests timing out?
- Verify provider is running (e.g., `curl http://localhost:11434/v1/models`)
- Check network connectivity
- Look for error messages in LLMResponse::error_message

**Phase 4**: Entities not executing LLM decisions?
- Verify AISys::set_llm_manager() was called in Game::start()
- Check LLMManager::is_available() returns true
- Inspect speech bubble to see if decision was made

**Phase 5**: UI not rendering?
- Ensure ImGui is initialized before render calls
- Check that systems are registered in game loop
- Look for ImGui warnings in console

**Phase 6**: Action not executing?
- Verify action reached `Phase::Complete`
- Check AIMemory for event recording
- Use describe() in speech bubble to see current phase

---

## Related Documentation

- **Global design doc**: `docs/llm-ai-system.md` (reference for full context)
- **CLAUDE.md**: Project build system and architecture
- **Code conventions**: C++20, EnTT ECS, Cereal serialization

---

## Checklist for Completion

When all 6 phases are complete, the game has:

- ✅ Dynamic action creation from LLM responses
- ✅ Unique entity personalities
- ✅ Persistent memory (events, conversations)
- ✅ Async LLM integration (any provider)
- ✅ Full game loop with LLM decisions
- ✅ ImGui visualization and configuration
- ✅ 13+ action types with state machines
- ✅ Automatic prerequisite resolution
- ✅ Graceful fallback when LLM unavailable
- ✅ Playable, feature-rich AI simulation

**Estimated Total Time**: 10-14 weeks, 1-2 developers

---

**Start with Phase 1 →** [Action System Foundation](./phase-1-action-system-foundation.md)
