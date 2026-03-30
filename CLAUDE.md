# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Dynamical is a C++20 Vulkan-based simulation engine featuring procedural terrain, water flow, AI entities with LLM-driven decision-making, and particle systems. It uses an ECS architecture powered by EnTT. The AI system supports both LLM-driven and score-based fallback decision modes, with personality, memory, and conversation systems.

**Current Status**: Phases 1-5 complete. Entities make decisions via LLM (Ollama, Claude, OpenAI), have unique personalities and memories, and display speech bubbles. Phase 6 (action implementations) in progress.

## Build System

Uses CMake with vcpkg for dependency management. Generator is Visual Studio 17 2022.

```bash
# Configure (vcpkg toolchain is set via CMakePresets.json)
cmake --preset default

# Build
cmake --build build

# The executable is output to the project root
```

Shaders (`.glsl` in `src/renderer/shaders/`) are compiled to SPIR-V via `glslangValidator` as part of the CMake build. Resources are copied to the build directory post-build.

On NixOS, a `flake.nix` development shell is provided.

## Architecture

### Game Loop (`src/logic/game.h`)
Entry point is `src/main.cpp` → `Game::start()`. The game loop runs: `renderer->prepare()` → `systems->pre_tick(dt)` → `systems->post_tick(dt)` → `renderer->render()`.

LLMManager is initialized in `Game::start()` and passed to AISys for async entity decisions.

### ECS Pattern
- **Registry**: Centralized `entt::registry` in `Game`
- **Components** (`src/logic/components/`): Plain data structs — Position, Object, BasicNeeds, Renderable, WaterFlow, Personality, AIMemory, etc.
- **Systems** (`src/logic/systems/`): Inherit from `System` base class with virtual `tick()`. Managed by `SystemSet` which handles pre/post tick ordering.
- **System registration**: Declared via macros in `system_list.h`

### Rendering (`src/renderer/`)
- **Triple buffering**: `NUM_FRAMES = 3` (in `context/num_frames.h`)
- **Semaphore chain**: Transfer → Compute → Graphics queues with per-frame synchronization
- **Context hierarchy**: Windu (SDL2 window) → Instance → Device (+ VMA allocator) → Swapchain → ClassicRender
- **Terrain shader**: Uses a GPU-side hash table mapping chunk coordinates to tile buffer indices (`maprender.vert.glsl`)

### Map/Terrain (`src/logic/map/`)
- **Chunks**: 32×32 tile grids, lazy-generated via `MapManager`
- **Tiles**: Type enum (dirt, grass, stone, sand, water, river), height, optional entity reference
- **Position component**: Managed exclusively by `MapManager` — do not modify directly
- **Procedural generation**: Uses FastNoise2 submodule

### AI System — Multi-Phase Architecture

#### Phase 1: Action System Foundation
**Files**: `src/ai/action_id.h`, `src/ai/action_registry.h`, `src/ai/action_base.h`, `src/ai/action_stage.h`

- **ActionRegistry**: Factory pattern for dynamic action creation from LLM responses. Registered via `REGISTER_ACTION_TYPE(ActionName)` macro in `.cpp` files.
- **ActionBase**: Base class for all actions with `tick(dt)`, `describe()`, `failure_reason`, prerequisites tracking.
- **ActionStageMachine** (`action_stage.h`): Named-stage state machine replacing raw `int phase`. Reference: `HarvestAction`, `EatAction`.
  - Use `.stage(name, lambda)` builder pattern
  - Lambda stages must use `this->reg`, `this->entity` (MSVC requirement for inherited members)
  - Built-in primitives: `wait_for_seconds()`, `wait_until_removed<C>()`, `wait_until_present<C>()`

#### Phase 2: Personality, Memory, Conversation
**Files**: `src/ai/personality/`, `src/ai/memory/`, `src/ai/conversation/`

- **Personality component** (`src/ai/personality/personality.h`):
  - Struct with archetype, speech_style, motivation, 4-6 traits
  - `PersonalityGenerator` uses seeded RNG for reproducible generation
  - `Personality::to_prompt_text()` serializes for LLM prompts
  - Stored as ECS component: `reg.get<Personality>(entity)`

- **AIMemory component** (`src/ai/memory/ai_memory.h`):
  - Tracks timestamped events (ISO 8601 format)
  - MemoryEvent: type, description, timestamp, seen_by_llm flag
  - Automatic event recording in `AISys::tick()` (action failures/completions)
  - Stored as ECS component: `reg.get<AIMemory>(entity)`

- **ConversationManager** (`src/ai/conversation/conversation_manager.h`):
  - Manages two-way conversations between entities
  - Stored in `Game` class member (NOT in registry)
  - Accessed via `game.conversation_manager`
  - Auto-concludes at max_messages, retains history

#### Phase 3: LLM Infrastructure
**Files**: `src/llm/` (llm_client, llm_manager, prompt_builder, response_parser, decision_cache)

- **LLMManager** (`src/llm/llm_manager.h`):
  - Async request queue with worker threads
  - Supports multiple providers: Ollama, LM Studio, Claude, OpenAI
  - Configured via `config.5.json` (provider, model, endpoint, api_key)
  - `is_available()` returns true if provider is reachable
  - Methods: `request_decision()`, `poll_result()`, `get_result()`

- **PromptBuilder** (`src/llm/prompt_builder.h`):
  - Constructs system + user prompts from entity context
  - Includes: Personality, AIMemory, current needs, available actions, conversation history

- **ResponseParser** (`src/llm/response_parser.h`):
  - Parses LLM text into ActionID + parameters
  - Extracts decision rationale and speech

- **DecisionCache** (`src/llm/decision_cache.h`):
  - Caches decisions by personality seed + state hash
  - Reduces redundant LLM calls for similar situations

#### Phase 4: Game Integration
**AISys** (`src/ai/ai.h/cpp`) now runs in **dual-mode**:

- **LLM mode** (when LLMManager available):
  - Submits entity context to LLM
  - Queues returned actions for execution
  - Polls for results non-blocking
  - Falls back automatically if LLM unavailable

- **Fallback mode** (score-based):
  - Uses existing behavior/decision system
  - Seamless degradation if LLM disabled or unreachable

**AIC component** (`src/ai/aic.h`) fields:
- `std::unique_ptr<Action> action` — currently executing action
- `std::queue<std::unique_ptr<Action>> action_queue` — LLM-queued actions
- `bool use_llm` — enable/disable LLM for entity
- `uint64_t pending_llm_request_id` — tracks in-flight request
- `bool waiting_for_llm` — true if request pending

#### Phase 5: UI & Visualization
**Files**: `src/ai/speech/`, `src/logic/systems/llm_debug.cpp`, `src/logic/systems/personality_inspector.cpp`

- **SpeechBubbleSys** (`src/ai/speech/speech_bubble_sys.h`):
  - Renders world-space speech bubbles above entities
  - Shows LLM decision, rationale, and action description
  - Auto-fades after fixed duration

- **LLMDebug system**:
  - ImGui panel for LLM configuration (provider, model, endpoint)
  - Shows pending requests and response times
  - Runtime provider switching

- **PersonalityInspector system**:
  - ImGui panel displaying entity personality, traits, motivation
  - Shows memory events with timestamps
  - Conversation history viewer

### Behaviors & Scoring (`src/ai/behaviors/`)
- **Behaviors**: Score-based (e.g., WanderBehavior, EatBehavior, HarvestBehavior)
- Score provides fallback decision when LLM unavailable
- LLM decisions override behavior scores

### Configuration
- Settings serialized to `./config.5.json` via Cereal (JSON)
- LLM configuration: provider (ollama/lm_studio/claude/openai), model name, endpoint, API key, rate limit, cache enabled
- Per-map settings stored in same file

## Key Dependencies

**vcpkg**: cereal, glm, sdl2[vulkan], taskflow, stb, vulkan
**Git submodules** (`lib/`):
- FastNoise2 (terrain generation)
- VulkanMemoryAllocator (VRAM management)
- ImGui, ImPlot (UI)
- EnTT (ECS)
- ai-sdk-cpp (LLM integration — Claude, OpenAI, etc.)

## Code Conventions

- C++20 standard
- Optick profiler integration (`OPTICK_EVENT` macros) — currently disabled in main
- `ThreadSafe<T>` wrapper in `src/util/util.h` for mutex-guarded shared state
- Taskflow executor used for parallel system execution

### ActionStageMachine Pattern (Phase 1+)
When writing new actions, use `ActionStageMachine` with named stages:

```cpp
class MyAction : public ActionBase {
  ActionStageMachine machine;
  MyAction(entt::registry& r, entt::entity e) : ActionBase(r, e) {
    machine.stage("init", [this](float dt) { /* ... */ })
           .stage("work", [this](float dt) { /* ... */ })
           .stage("finish", [this](float dt) { machine.finish(); });
  }
  void tick(float dt) override { machine.tick(dt); }
  Phase get_phase() const override { return machine.get_phase(); }
  std::string describe() const override { return machine.current_stage_name(); }
};
```

**Important**: Use `this->reg`, `this->entity` inside lambda bodies (MSVC C3493 requirement).

### Personality & Memory Usage (Phase 2+)
When accessing entity context:

```cpp
auto& personality = reg.get<Personality>(entity);
auto& memory = reg.get<AIMemory>(entity);
memory.record_event("action_completed", "Harvested 5 wheat", true);
```

### LLM Request Pattern (Phase 3+)
PromptBuilder constructs context; ResponseParser handles results:

```cpp
// In AISys::submit_llm_request()
auto context = build_prompt_context(entity);
auto request_id = llm_manager->request_decision(context);
aic.pending_llm_request_id = request_id;
aic.waiting_for_llm = true;
```
