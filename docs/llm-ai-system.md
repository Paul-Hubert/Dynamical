# LLM-Driven AI System — Design Document

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current System Analysis](#2-current-system-analysis)
3. [Architecture Overview](#3-architecture-overview)
4. [LLM Provider Abstraction](#4-llm-provider-abstraction)
5. [Action System Redesign](#5-action-system-redesign)
6. [Prerequisite Resolution](#6-prerequisite-resolution)
7. [Async LLM Integration](#7-async-llm-integration)
8. [Prompt Construction](#8-prompt-construction)
9. [Action Skeleton Reference](#9-action-skeleton-reference)
10. [Speech Bubbles & Action Display](#10-speech-bubbles--action-display)
11. [LLM Configuration UI](#11-llm-configuration-ui)
12. [Migration Path](#12-migration-path)
13. [File Structure](#13-file-structure)
14. [Performance](#14-performance)
15. [Problems & Solutions](#15-problems--solutions)
16. [Implementation Phases](#16-implementation-phases)

---

## 1. Executive Summary

This document describes a redesign of the Dynamical AI system to replace the current
score-based behavior selection with LLM-driven decision making. Instead of hardcoded
scoring functions (e.g. "if hunger > 5, eat"), each AI entity will query a language model
— running locally via **ollama** or **LM Studio**, or remotely via the **Claude API** —
to decide what actions to take.

The LLM acts as a **planner**, not a **controller**. It receives a snapshot of the
entity's state and returns a prioritized list of recognized action names. The game engine
then executes those actions through the existing action/phase state machine, automatically
resolving prerequisites (e.g. gathering food before eating). This keeps the simulation
deterministic and robust — the LLM cannot produce invalid game state, only choose from
valid actions.

Key design goals:

- **Non-blocking**: LLM calls happen on worker threads; entities wander while waiting.
- **Graceful fallback**: When the LLM is unavailable, the existing score-based system runs.
- **Switchable providers**: Change between ollama, LM Studio, and Claude at runtime.
- **Declarative actions**: New actions are trivial to add via a CRTP template pattern.
- **Automatic prerequisites**: A recursive resolver chains dependent actions automatically.
- **Visual feedback**: ImGui speech bubbles show entity thoughts, dialogue, and current action.

---

## 2. Current System Analysis

### 2.1 How AI Decisions Work Today

The current system lives in `src/ai/` and follows a simple pattern:

```
AISys::tick()
  └─ For each entity with AIC component:
       ├─ If action == nullptr OR action->interruptible:
       │    └─ decide() → score all behaviors, pick highest
       └─ Execute: ai.action = ai.action->act(move(ai.action))
```

**AIC component** (`src/ai/aic.h`): Holds `unique_ptr<Action> action` and `float score`.

**Behaviors** (`src/ai/behaviors/`): Each has a static `getScore()` method:
- `WanderBehavior` — always returns 1 (fallback)
- `EatBehavior` — returns `hunger` if > 5, else 0

**Actions** (`src/ai/actions/`): State machines that return `unique_ptr<Action>` from `act()`:
- `WanderAction` — picks random direction, creates Path component, interruptible
- `EatAction` — checks storage for food, emits Eat component, waits for EatSys
- `HarvestAction` — finds plant, pathfinds to it, reserves it, emits Harvest component

### 2.2 The Action Chain Pattern

Actions form a **linked list** via `unique_ptr<Action> next`. The key method is:

```cpp
void link(Action* before, std::unique_ptr<Action> after) {
    before->next = std::move(after);
}
std::unique_ptr<Action> nextAction() {
    if (next) return next->act(std::move(next));
    return nullptr;
}
```

`EatAction::deploy()` demonstrates manual prerequisite chaining: it creates a
`HarvestAction`, links `EatAction` as its successor, and returns the `HarvestAction` as
the head of the chain. This works but requires each action to manually know its
prerequisites.

### 2.3 What This System Lacks

| Limitation | Impact |
|------------|--------|
| Hardcoded scoring | Adding new behaviors requires modifying `ai.cpp` |
| Manual prerequisite chaining | Each action must know how to create its own dependencies |
| No emergent behavior | Entities follow rigid if/then logic |
| No communication | Entities cannot talk to each other |
| No memory | Entities don't remember past actions or events |
| No personality | All entities of the same type behave identically |

---

## 3. Architecture Overview

### 3.1 High-Level Data Flow

```
┌──────────────────────────────────────────────────────────┐
│                     GAME LOOP (per frame)                 │
│                                                          │
│  ┌─────────┐    ┌──────────┐    ┌───────────┐           │
│  │ Systems  │───>│  AISys   │───>│  Renderer │           │
│  │ pre_tick │    │  tick()  │    │  render() │           │
│  └─────────┘    └────┬─────┘    └───────────┘           │
│                      │                                    │
│         ┌────────────┼────────────┐                      │
│         │            │            │                      │
│         ▼            ▼            ▼                      │
│  ┌────────────┐ ┌─────────┐ ┌──────────┐               │
│  │ Poll LLM   │ │ Execute │ │ Submit   │               │
│  │ Results    │ │ Actions │ │ Requests │               │
│  └─────┬──────┘ └─────────┘ └────┬─────┘               │
│        │                          │                      │
└────────┼──────────────────────────┼──────────────────────┘
         │                          │
         │    ┌──────────────────┐  │
         └───>│   LLMManager     │<─┘
              │  (worker threads) │
              │                  │
              │  request_queue ──┼──> Worker Thread 1 ──> HTTP ──> LLM
              │  result_queue  <─┼──  Worker Thread 2 ──> HTTP ──> LLM
              │  cache           │
              └──────────────────┘
                       │
              ┌────────┼────────┐
              ▼        ▼        ▼
          ┌───────┐ ┌───────┐ ┌────────┐
          │Ollama │ │LM     │ │Claude  │
          │:11434 │ │Studio │ │API     │
          └───────┘ │:1234  │ └────────┘
                    └───────┘
```

### 3.2 Decision Flow for a Single Entity

```
1. AISys::tick() finds entity with empty action queue and no pending LLM request
2. PromptBuilder serializes entity state → prompt string
3. LLMManager::submit() enqueues the request (non-blocking)
4. Entity wanders (fallback) while waiting
5. Worker thread picks up request, calls active LLM provider
6. LLM returns JSON: {"thought": "I'm hungry...", "actions": ["Eat", "Explore"]}
7. LLMManager parses response → LLMDecisionResult with action_queue
8. Next frame: AISys::tick() polls results, finds this entity's result
9. AIC::action_queue = [Eat, Explore]
10. Pop Eat → PrerequisiteResolver checks: has food? No → prepend Harvest
11. Chain: Harvest → Eat created via link()
12. Entity executes Harvest phases (pathfind → reserve → harvest)
13. Harvest completes → chain advances to Eat
14. Eat completes → pop Explore from queue
15. Explore executes...
16. Queue empty → submit new LLM request → cycle repeats
```

### 3.3 Dual-Mode Decision Logic

The modified `AISys::tick()` runs in two modes:

**LLM Mode** (when `AIC::use_llm == true` and LLM is available):
- Actions come from the LLM-assigned `action_queue`
- Prerequisites are resolved automatically by `PrerequisiteResolver`
- When queue is empty, a new LLM request is submitted
- Entity wanders while waiting for LLM response

**Fallback Mode** (when LLM is unavailable or `use_llm == false`):
- Runs the existing `decide()` method unchanged
- Score-based behavior selection: WanderBehavior, EatBehavior, etc.
- Zero LLM dependency — the game works identically to today

The switch is per-entity: humans can use LLM while animals use fallback, or all entities
can use fallback if the LLM server is down.

---

## 4. LLM Provider Abstraction

### 4.1 Provider Interface

```cpp
class LLMProvider {
public:
    virtual ~LLMProvider() = default;
    virtual ProviderType type() const = 0;
    virtual std::string name() const = 0;
    virtual LLMResponse complete(const LLMRequest& request) = 0;
    virtual bool is_available() = 0;
};
```

`LLMRequest` contains: `prompt`, `system_prompt`, `temperature`, `max_tokens`.
`LLMResponse` contains: `success`, `content`, `error_message`, `tokens_used`, `latency_ms`.

Each provider implements `complete()` as a **synchronous** HTTP call. This is safe because
providers are only called from worker threads, never from the game loop.

### 4.2 Ollama Provider

- **Endpoint**: `POST http://localhost:11434/api/generate`
- **Request format**: `{"model": "llama3.2", "prompt": "...", "system": "...", "stream": false, "options": {"temperature": 0.7, "num_predict": 256}}`
- **Response format**: `{"response": "...", "total_duration": ..., "eval_count": ...}`
- **Availability check**: `GET http://localhost:11434/api/tags` (returns model list)
- **Model selection**: Configurable via settings (default: "llama3.2")
- **Notes**: Setting `"stream": false` returns the full response in one JSON object, avoiding streaming complexity.

### 4.3 LM Studio Provider

- **Endpoint**: `POST http://localhost:1234/v1/chat/completions` (OpenAI-compatible)
- **Request format**: `{"messages": [{"role": "system", "content": "..."}, {"role": "user", "content": "..."}], "temperature": 0.7, "max_tokens": 256}`
- **Response format**: `{"choices": [{"message": {"content": "..."}}], "usage": {"total_tokens": ...}}`
- **Availability check**: `GET http://localhost:1234/v1/models`
- **Notes**: Uses the OpenAI-compatible API that LM Studio exposes. Any model loaded in LM Studio is used automatically.

### 4.4 Claude API Provider

- **Endpoint**: `POST https://api.anthropic.com/v1/messages`
- **Request format**: `{"model": "claude-sonnet-4-20250514", "max_tokens": 256, "system": "...", "messages": [{"role": "user", "content": "..."}]}`
- **Response format**: `{"content": [{"text": "..."}], "usage": {"input_tokens": ..., "output_tokens": ...}}`
- **Headers**: `x-api-key: <key>`, `anthropic-version: 2023-06-01`, `content-type: application/json`
- **Availability check**: Try a minimal request; check for 200 status.
- **API key**: Stored in settings (loaded from `config.5.json`), never committed to git.
- **Notes**: Requires HTTPS. The `cpp-httplib` library supports SSL when built with `[ssl]` feature in vcpkg.

### 4.5 Runtime Switching

The `LLMManager` holds one instance of each provider. Switching is instantaneous:

```cpp
void LLMManager::set_provider(ProviderType type) {
    active = type;
    // In-flight requests on old provider complete normally.
    // New requests use the new provider.
}
```

No requests are dropped during a switch. In-flight requests on the old provider still
complete and their results are delivered normally. Only new submissions use the new provider.

### 4.6 New Dependency: cpp-httplib

Add to `vcpkg.json`:
```json
"cpp-httplib"
```

Add to `CMakeLists.txt`:
```cmake
find_package(httplib CONFIG REQUIRED)
target_link_libraries(dynamical PRIVATE httplib::httplib)
```

cpp-httplib is a header-only HTTP/HTTPS client+server library. It supports SSL via OpenSSL
(needed for Claude API). It is the lightest-weight option available in vcpkg for HTTP.

---

## 5. Action System Redesign

### 5.1 ActionID Enum

A central enum identifying every action type in the game:

```cpp
enum class ActionID : int {
    None = 0,
    Wander, Eat, Harvest, Mine, Hunt, Build, Sleep,
    Trade, Talk, Craft, Fish, Explore, Flee, Store, Retrieve
};
```

Every action has a unique ID used for: prerequisite references, LLM response parsing,
caching, and logging.

### 5.2 ActionDescriptor

Metadata about each action type, stored in the `ActionRegistry`:

```cpp
struct ActionPrerequisite {
    std::function<bool(entt::registry&, entt::entity)> is_satisfied;
    std::function<ActionID(entt::registry&, entt::entity)> resolve_action;
    std::string description;  // e.g., "needs food in inventory"
};

struct ActionDescriptor {
    ActionID id;
    std::string name;           // "Eat" — matches LLM output
    std::string description;    // "Consume food to reduce hunger"
    std::vector<ActionPrerequisite> prerequisites;
    std::function<std::unique_ptr<Action>(entt::registry&, entt::entity)> create;
};
```

Prerequisites are **runtime-checked functions**, not static declarations. This allows
context-sensitive resolution: "Craft requires Wood" might resolve to Harvest (chop tree)
or Trade (buy from another entity) depending on what's nearby.

### 5.3 ActionRegistry

Singleton that holds all registered `ActionDescriptor` objects:

```cpp
class ActionRegistry {
public:
    static ActionRegistry& instance();
    void register_action(ActionDescriptor descriptor);
    const ActionDescriptor* get(ActionID id) const;
    std::vector<const ActionDescriptor*> get_all() const;
    std::vector<const ActionDescriptor*> get_available(entt::registry& reg, entt::entity entity) const;
};
```

`get_available()` filters actions based on entity capabilities. For example, only entities
with a `BasicNeeds` component can Eat; only entities with a `Builder` component can Build.

### 5.4 CRTP Action Factory

The `ActionBase` template eliminates boilerplate for new actions:

```cpp
template<typename Derived, ActionID ID>
class ActionBase : public Action {
public:
    using Action::Action;
    ActionID id() const override { return ID; }

    // Auto-registration via static initializer
    static bool registered;
    static bool register_action() {
        ActionRegistry::instance().register_action(ActionDescriptor{
            .id = ID,
            .name = Derived::action_name(),
            .description = Derived::action_description(),
            .prerequisites = Derived::get_prerequisites(),
            .create = [](entt::registry& r, entt::entity e) {
                return std::make_unique<Derived>(r, e);
            }
        });
        return true;
    }
};

// In each action's .cpp file:
// template<> bool ActionBase<EatAction, ActionID::Eat>::registered = register_action();
```

**To create a new action, you implement:**
1. A class inheriting `ActionBase<MyAction, ActionID::MyAction>`
2. `static const char* action_name()` — name string (matches LLM output)
3. `static const char* action_description()` — for LLM prompt context
4. `static std::vector<ActionPrerequisite> get_prerequisites()` — dependency declarations
5. `std::string describe() const override` — human-readable current state (for speech bubble)
6. `std::unique_ptr<Action> act(std::unique_ptr<Action> self) override` — the state machine

No manual registration, no modification of `ai.cpp`, no changes to any other file.

### 5.5 Modified Action Base Class

The existing `Action` class gains two virtual methods:

```cpp
class Action {
public:
    // ... existing members unchanged ...
    virtual ActionID id() const = 0;
    virtual std::string describe() const = 0;

    // Existing:
    virtual std::unique_ptr<Action> act(std::unique_ptr<Action> self) = 0;
    void link(Action* before, std::unique_ptr<Action> after);
    std::unique_ptr<Action> nextAction();
    bool interruptible = false;
};
```

---

## 6. Prerequisite Resolution

### 6.1 The Problem

Currently, `EatAction::deploy()` manually creates a `HarvestAction` and chains itself
after it. This means:
- Every action must know its own prerequisites.
- Adding a new prerequisite means modifying the action's code.
- Complex chains (Craft → need Wood → Harvest tree OR Trade for Wood) require complex
  conditional logic inside each action.

### 6.2 The Recursive Resolver

The `PrerequisiteResolver` externalizes this logic:

```cpp
class PrerequisiteResolver {
public:
    std::unique_ptr<Action> resolve(
        ActionID target,
        entt::registry& reg,
        entt::entity entity,
        int max_depth = 5
    );
};
```

**Algorithm:**

```
resolve(target, entity, depth=0, visited={}):
    if depth > max_depth: return create(target)  // safety limit
    if target in visited: return create(target)   // cycle break

    visited.add(target)
    descriptor = registry.get(target)
    chain_head = nullptr
    chain_tail = nullptr

    for each prerequisite in descriptor.prerequisites:
        if not prerequisite.is_satisfied(reg, entity):
            prereq_id = prerequisite.resolve_action(reg, entity)
            prereq_chain = resolve(prereq_id, entity, depth+1, visited)
            append prereq_chain to chain

    target_action = descriptor.create(reg, entity)
    append target_action to chain
    return chain_head (or target_action if no prerequisites needed)
```

### 6.3 Example: Eating When Hungry

```
resolve(Eat)
  ├─ Check prerequisite: "has food in inventory"
  │   └─ NOT satisfied (storage empty)
  │   └─ resolve_action returns: Harvest
  │       └─ resolve(Harvest)
  │           ├─ Check prerequisite: "nearby harvestable plant"
  │           │   └─ IS satisfied (berry bush nearby)
  │           └─ Create HarvestAction
  └─ Create EatAction
  └─ Chain: HarvestAction → EatAction
```

### 6.4 Example: Crafting a Tool

```
resolve(Craft)
  ├─ Check: "has required materials"
  │   └─ NOT satisfied (need wood + stone)
  │   └─ resolve_action checks: wood available? → Harvest (chop tree)
  │       └─ resolve(Harvest)
  │           └─ Create HarvestAction(target=tree)
  ├─ Check: "has required materials" (stone)
  │   └─ NOT satisfied (need stone)
  │   └─ resolve_action checks: stone nearby? → Mine
  │       └─ resolve(Mine)
  │           └─ Create MineAction
  └─ Create CraftAction
  └─ Chain: HarvestAction → MineAction → CraftAction
```

### 6.5 Cycle Detection and Depth Limiting

The `visited` set prevents cycles like: Trade→needs gold→Mine→needs pickaxe→Craft→needs iron→Trade...

The `max_depth` limit (default 5) prevents pathological chains. If depth is exceeded, the
target action is created directly — it will likely fail, and the failure triggers a new LLM
decision with updated context ("Craft failed: missing materials").

### 6.6 Dynamic Resolution

Prerequisites use `std::function`, allowing runtime decisions:

```cpp
// Craft's prerequisite for wood:
ActionPrerequisite{
    .is_satisfied = [](auto& reg, auto e) {
        return reg.get<Storage>(e).amount(Item::wood) >= 2;
    },
    .resolve_action = [](auto& reg, auto e) {
        // Check if there's a tree nearby → Harvest
        // Otherwise → Trade
        auto* mm = &reg.ctx<MapManager>();
        if (mm->findNearest(e, Object::tree))
            return ActionID::Harvest;
        else
            return ActionID::Trade;
    },
    .description = "needs 2 wood"
}
```

This allows the same prerequisite to resolve differently based on world state.

---

## 7. Async LLM Integration

### 7.1 Why Async Is Essential

LLM inference takes 200ms–2000ms+ depending on model size and hardware. The game loop
runs at 60fps (16ms per frame). A synchronous LLM call would freeze the game for
12–125 frames. This is unacceptable.

### 7.2 LLMManager Architecture

```cpp
class LLMManager {
    // Provider instances (one per type, initialized at startup)
    std::unique_ptr<LLMProvider> providers[3];
    ProviderType active;

    // Thread-safe queues (using existing ThreadSafe<T> from src/util/util.h)
    ThreadSafe<std::deque<LLMDecisionRequest>> request_queue;
    ThreadSafe<std::vector<LLMDecisionResult>> result_queue;

    // Worker thread pool
    std::vector<std::thread> workers;
    std::atomic<bool> running{true};
    std::condition_variable work_available;
    std::mutex work_mutex;

    // Rate limiting
    int rate_limit_rps = 5;
    int max_concurrent = 2;
    std::atomic<int> active_requests{0};

    // Cache
    DecisionCache cache;
};
```

### 7.3 Request Lifecycle

```
Frame N:   AISys detects entity needs decision
           → PromptBuilder constructs prompt
           → LLMManager::submit(request)         [enqueues, returns immediately]
           → Entity continues wandering

Frame N+1: Worker thread dequeues request
           → Checks cache (hit? return cached result)
           → Checks rate limit (exceeded? wait or skip)
           → Calls provider.complete(request)     [blocks worker thread, NOT game]
           → Parses response → LLMDecisionResult
           → Enqueues result in result_queue

Frame N+K: AISys calls LLMManager::poll_results()
           → Dequeues all completed results
           → Matches results to entities (validates entity still exists)
           → Sets AIC::action_queue
           → Entity starts executing LLM-chosen actions
```

K is typically 12–120 frames (200ms–2000ms at 60fps). During this time the entity
wanders naturally — there is no visible "thinking freeze."

### 7.4 Worker Thread Loop

```
worker_loop():
    while (running):
        wait on work_available condition variable

        lock request_queue
        if queue is empty: continue
        request = queue.pop_front()
        unlock

        // Rate limiting
        while active_requests >= max_concurrent:
            sleep 10ms
        active_requests++

        // Cache check
        cache_key = compute_cache_key(request)
        cached = cache.lookup(cache_key)
        if cached:
            result = make_result(request.entity, cached.actions, cached.thought)
            result.from_cache = true
        else:
            response = providers[active]->complete(request)
            result = parse_response(request.entity, response)
            cache.insert(cache_key, result)

        active_requests--

        lock result_queue
        result_queue.push_back(result)
        unlock
```

### 7.5 Entity Validity After Async Delay

Between submission and result delivery, an entity might be destroyed (died, despawned).
The result handler must check:

```cpp
auto results = llm.poll_results();
for (auto& result : results) {
    if (!reg.valid(result.entity)) continue;          // Entity was destroyed
    if (!reg.all_of<AIC>(result.entity)) continue;    // AIC was removed
    auto& ai = reg.get<AIC>(result.entity);
    if (!ai.waiting_for_llm) continue;                // Stale result (entity already got a decision)
    // ... apply result ...
}
```

### 7.6 Shutdown

```cpp
LLMManager::~LLMManager() {
    running = false;
    work_available.notify_all();
    for (auto& t : workers) t.join();
}
```

Worker threads check `running` flag and exit cleanly. In-flight HTTP requests will
complete or timeout (cpp-httplib supports timeout configuration).

---

## 8. Prompt Construction

### 8.1 System Prompt (Constant)

The system prompt is sent with every request and defines the LLM's role:

```
You are an AI controlling an entity in a village simulation. Based on the entity's
current state and surroundings, decide what actions to take next.

RULES:
- Respond ONLY with a JSON object. No other text.
- Choose from the listed available actions only.
- "actions" is an ordered list (first = highest priority).
- "thought" is your brief internal reasoning (shown as thought bubble).
- "dialogue" is optional speech (shown as speech bubble to nearby entities).
- Prerequisites are handled automatically — just choose the high-level goal.
  For example, say "Eat" not "Harvest then Eat".

RESPONSE FORMAT:
{
  "thought": "I'm getting hungry and there's food nearby",
  "actions": ["Eat", "Explore"],
  "dialogue": ""
}
```

### 8.2 Entity State Prompt (Dynamic)

Built per-entity by `PromptBuilder::build_decision_prompt()`:

```
=== YOUR STATE ===
Type: Human
Position: (45.2, 23.1) in a grassland area
Hunger: 7.3/10 (getting hungry)
Inventory: berry x2, wood x1
Current action: None (idle)

=== NEARBY ENTITIES ===
- Berry Bush at (44.0, 25.0) — 2.3 tiles away
- Human "Entity#42" at (46.0, 22.0) — wandering, hunger 3.1/10
- Stone deposit at (48.0, 20.0) — 4.2 tiles away
- Tree at (43.0, 24.0) — 2.5 tiles away

=== RECENT EVENTS ===
- Completed: Harvest (gathered berries) — 30s ago
- Completed: Wander — 45s ago
- Failed: Hunt (no prey nearby) — 2min ago

=== AVAILABLE ACTIONS ===
- Wander: Walk to a random nearby location
- Eat: Consume food to reduce hunger
- Harvest: Gather resources from nearby plants
- Mine: Extract stone or ore from deposits
- Hunt: Chase and kill animals for food/materials
- Build: Construct a structure at current location
- Sleep: Rest to restore energy
- Trade: Exchange items with a nearby entity
- Talk: Speak with a nearby entity
- Craft: Create items from raw materials
- Fish: Catch fish from nearby water
- Explore: Scout unknown territory
- Flee: Run away from danger
- Store: Place items in a storage container
- Retrieve: Take items from a storage container

What should this entity do?
```

### 8.3 Prompt Size Management

| Component | Approximate tokens |
|-----------|--------------------|
| System prompt | ~150 |
| Entity state | ~50 |
| Nearby entities (up to 8) | ~120 |
| Recent events (up to 5) | ~80 |
| Available actions (15) | ~200 |
| **Total** | **~600 tokens** |

This is well within limits for all providers (ollama context windows typically 2K–8K,
Claude supports 200K). The prompt is intentionally compact.

### 8.4 Nearby Entity Serialization

Only entities within a radius (default 15 tiles) are included. The list is capped at
8 entries, sorted by distance. For each nearby entity:
- Type (Human, animal, plant, structure)
- Position and distance
- Current action (if visible)
- Key state (hunger level for humans, harvestable status for plants)

### 8.5 Response Parsing

The LLM response is expected to be a JSON object. Parsing strategy:

1. **Try JSON parse** (using a simple extractor or cereal).
2. **Extract fields**: `thought` (string), `actions` (array of strings), `dialogue` (string).
3. **Map action names to ActionID** via `ActionRegistry`. Unknown names are silently dropped.
4. **Validate**: If no valid actions remain, default to `[Wander]`.
5. **Fallback for malformed responses**: If JSON parsing fails entirely, default to `[Wander]` and log the error.

The system is robust to LLM hallucination because only recognized action names pass
through. An LLM that says `"actions": ["Fly", "Teleport"]` simply gets `[Wander]`.

---

## 9. Action Skeleton Reference

### 9.1 Complete Action Table

| ActionID | Name | Description | Prerequisites | Interruptible | Implementation Status |
|----------|------|-------------|---------------|---------------|----------------------|
| Wander | Wander | Walk to a random nearby location | None | Yes | **Existing** |
| Eat | Eat | Consume food to reduce hunger | Has food in storage → Harvest | No | **Existing** |
| Harvest | Harvest | Gather resources from nearby plants/trees | Harvestable object nearby (soft prereq) | No | **Existing** |
| Mine | Mine | Extract stone/ore from rock deposits | Mineable deposit nearby (soft prereq) | No | Skeleton |
| Hunt | Hunt | Chase and kill animals for food/materials | Prey animal nearby (soft prereq) | No | Skeleton |
| Build | Build | Construct a structure | Has building materials → Harvest/Mine/Trade | No | Skeleton |
| Sleep | Sleep | Rest to restore energy | None | Yes (after falling asleep, no) | Skeleton |
| Trade | Trade | Exchange items with another entity | Nearby entity willing to trade | No | Skeleton |
| Talk | Talk | Speak with a nearby entity | Nearby entity | No | Skeleton |
| Craft | Craft | Create items from raw materials | Has required materials → Harvest/Mine/Trade | No | Skeleton |
| Fish | Fish | Catch fish from nearby water | Water tile nearby (soft prereq) | No | Skeleton |
| Explore | Explore | Scout unknown territory | None | Yes | Skeleton |
| Flee | Flee | Run away from danger | None | No | Skeleton |
| Store | Store | Place items in a storage container | Has items + storage nearby | No | Skeleton |
| Retrieve | Retrieve | Take items from a storage container | Storage nearby with items | No | Skeleton |

**Soft prerequisite**: A condition the action checks internally (e.g., "is there a rock nearby?"). If not met, the action fails immediately and triggers a new LLM decision with updated context.

**Hard prerequisite**: Declared in `get_prerequisites()`, automatically resolved by the `PrerequisiteResolver` before the action starts.

### 9.2 Prerequisite Dependency Graph

```
Eat ──────────► Has food? ──NO──► Harvest (berries/plants)
                                  OR Hunt (meat)

Craft ────────► Has materials? ──NO──► Harvest (wood, fiber)
                                       OR Mine (stone, ore)
                                       OR Trade (buy materials)

Build ────────► Has building materials? ──NO──► Craft (planks, bricks)
                                                OR Harvest (raw wood)
                                                OR Mine (raw stone)

Trade ────────► Nearby willing entity? (soft prereq, no auto-resolve)

Talk ─────────► Nearby entity? (soft prereq)

Hunt ─────────► Nearby prey? (soft prereq)

Mine ─────────► Nearby deposit? (soft prereq)

Fish ─────────► Nearby water? (soft prereq)

Store ────────► Has items + nearby storage? (soft prereq)

Retrieve ─────► Nearby storage with items? (soft prereq)

Wander ───────► (no prerequisites)
Explore ──────► (no prerequisites)
Flee ─────────► (no prerequisites)
Sleep ────────► (no prerequisites)
```

### 9.3 Action Phase Outlines

Each action is a state machine with phases. These are design outlines, not implementation.

**MineAction**
- Phase 0: Find nearest mineable deposit via MapManager. Pathfind to it. Reserve with tag.
- Phase 1: Wait for entity to arrive (Path component consumed).
- Phase 2: Emit `Mine` component. Wait for `MineSys` to process (duration-based).
- Phase 3: Add mined resources to Storage. Remove reservation. Return `nextAction()`.

**HuntAction**
- Phase 0: Find nearest prey entity (bunny, etc.) via view query. Pathfind toward it.
- Phase 1: Track prey movement each tick (prey may move). Re-pathfind if needed.
- Phase 2: When adjacent to prey, emit `Attack` component. Wait for combat resolution.
- Phase 3: If kill successful, add meat/materials to Storage. Return `nextAction()`.
- Failure: If prey escapes (distance > threshold), fail → triggers new LLM decision.

**BuildAction**
- Phase 0: Select build location (from entity's build plan or nearby open tile).
- Phase 1: Pathfind to build location.
- Phase 2: Emit `Build` component with structure type. Wait for `BuildSys`.
- Phase 3: Structure entity created in world. Return `nextAction()`.

**SleepAction**
- Phase 0: Find safe location (or sleep in place). Optionally pathfind to shelter.
- Phase 1: Emit `Sleeping` component. Entity becomes non-interruptible.
- Phase 2: Wait for energy to restore (duration-based via Time component).
- Phase 3: Remove `Sleeping` component. Become interruptible. Return `nextAction()`.

**TradeAction**
- Phase 0: Find nearest entity with `Trader` component (or any human). Pathfind to them.
- Phase 1: Wait for arrival.
- Phase 2: Emit `TradeRequest` component on self, referencing target entity.
- Phase 3: Wait for target to accept (target's LLM receives "Entity#7 wants to trade" event).
- Phase 4: Exchange items via Storage components. Return `nextAction()`.
- Failure: Target rejects or moves away → fail.

**TalkAction**
- Phase 0: Find nearest entity. Pathfind to them.
- Phase 1: Wait for arrival.
- Phase 2: Generate dialogue via LLM (specialized prompt: "What do you say to Entity#42?").
- Phase 3: Set `AIMemory::current_dialogue` on self. Add "was spoken to by Entity#7" event to target's `AIMemory`.
- Phase 4: Wait for dialogue display duration. Return `nextAction()`.

**CraftAction**
- Phase 0: Check recipe requirements against Storage. (Prerequisites should have ensured materials exist.)
- Phase 1: Emit `Crafting` component with recipe type. Wait for `CraftSys`.
- Phase 2: Remove consumed materials, add crafted item to Storage. Return `nextAction()`.

**FishAction**
- Phase 0: Find nearest water tile. Pathfind to adjacent land tile.
- Phase 1: Wait for arrival.
- Phase 2: Emit `Fishing` component. Wait duration (with random success chance via Time).
- Phase 3: If successful, add fish to Storage. Return `nextAction()`.
- Failure: Random chance of catching nothing → triggers new LLM decision.

**ExploreAction**
- Phase 0: Pick a direction toward unexplored territory (chunks not yet generated or visited).
- Phase 1: Create long-distance Path toward target area.
- Phase 2: Walk path. Interruptible — LLM can redirect.
- Phase 3: When arrived, mark area as "explored" in AIMemory. Return `nextAction()`.

**FleeAction**
- Phase 0: Identify threat source (nearest hostile entity or danger).
- Phase 1: Pathfind in opposite direction from threat, maximum speed.
- Phase 2: Run until threat is far enough away (distance threshold).
- Phase 3: Return `nextAction()`.
- Non-interruptible during flee — survival takes priority.

**StoreAction**
- Phase 0: Find nearest storage structure (chest, warehouse entity).
- Phase 1: Pathfind to it.
- Phase 2: Transfer specified items from entity Storage to structure Storage.
- Phase 3: Return `nextAction()`.

**RetrieveAction**
- Phase 0: Find nearest storage structure with desired items.
- Phase 1: Pathfind to it.
- Phase 2: Transfer items from structure Storage to entity Storage.
- Phase 3: Return `nextAction()`.

---

## 10. Speech Bubbles & Action Display

### 10.1 Design

Each entity can display three types of text above them:
1. **Dialogue** (yellow): What the entity says aloud — `"I need some food"`
2. **Thought** (gray, parenthesized): Internal LLM reasoning — `(hungry, should eat)`
3. **Current action** (blue, bracketed): What the entity is doing — `[Harvesting berries]`

These appear as floating ImGui windows positioned above the entity in screen space.

### 10.2 AIMemory Component

```cpp
struct AIMemory {
    // Event history (for LLM context)
    std::vector<AIEvent> events;  // capped at 10

    // Display state (for speech bubbles)
    std::string current_thought;
    std::string current_dialogue;
    double dialogue_expires;      // game time when dialogue fades
};
```

### 10.3 SpeechBubbleSys

Registered in `game.cpp` as a pre-tick system (after AISys, before rendering). Follows
the exact pattern of the existing `ActionBarSys` which already renders ImGui overlays
at entity positions.

**Algorithm per frame:**
1. Get Camera and Input from registry context.
2. Iterate all entities with `AIMemory` + `Position`.
3. Convert entity world position to screen coordinates via `Camera::fromWorldSpace()`.
4. Frustum-cull: skip entities whose screen position is off-screen.
5. Render an ImGui window at `(screen_x - offset, screen_y - height_offset)` with:
   - Dialogue text (if present and not expired) in yellow
   - Thought text (if present) in dim gray
   - Current action description (from `AIC::action->describe()`) in blue
6. Use `ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize` for clean floating appearance.

### 10.4 Dialogue Expiration

Dialogue text has a time-to-live (e.g. 5 game-seconds). The `dialogue_expires` field
is set when dialogue is assigned:

```cpp
memory.current_dialogue = result.dialogue;
memory.dialogue_expires = time.current + 5 * Time::second;
```

The SpeechBubbleSys checks `time.current < memory.dialogue_expires` before rendering
dialogue. Thoughts persist until the next LLM decision updates them.

### 10.5 Performance Considerations for Speech Bubbles

- **Frustum culling** eliminates off-screen entities (usually 90%+ of all entities).
- ImGui window creation is cheap (~1 microsecond per bubble).
- Text rendering is batched by ImGui internally.
- For 100 visible entities, the overhead is ~0.1ms per frame — negligible.
- **Configurable**: A setting to disable speech bubbles entirely (for maximum performance).

---

## 11. LLM Configuration UI

### 11.1 ImGui Panel

Added to the existing `DevMenuSys` or as a standalone ImGui window:

```
┌─ LLM Configuration ──────────────────────┐
│                                           │
│  Provider: (●) Ollama ( ) LM Studio ( ) Claude │
│  Model: [llama3.2        ▼]              │
│  [Switch Provider]                        │
│                                           │
│  ── Status ──                             │
│  Connection: ● Connected                  │
│  Pending requests: 3                      │
│  Avg latency: 342ms                       │
│  Cache hit rate: 67% (124/185)            │
│                                           │
│  ── Settings ──                           │
│  Max RPS: [====5=====] 1-20              │
│  Max concurrent: [==2===] 1-8            │
│  Temperature: [===0.7===] 0.0-1.0        │
│  Entity LLM toggle: [x] Humans [ ] Animals│
│                                           │
│  [Clear Cache] [Test Connection]          │
└───────────────────────────────────────────┘
```

### 11.2 Settings Persistence

LLM settings are added to the existing `Settings` class and serialized via cereal:

```cpp
struct LLMSettings {
    int active_provider = 0;  // 0=Ollama, 1=LMStudio, 2=Claude
    std::string ollama_model = "llama3.2";
    std::string ollama_host = "localhost";
    int ollama_port = 11434;
    std::string lmstudio_host = "localhost";
    int lmstudio_port = 1234;
    std::string claude_api_key = "";  // WARNING: stored locally only
    std::string claude_model = "claude-sonnet-4-20250514";
    int rate_limit_rps = 5;
    int max_concurrent = 2;
    float temperature = 0.7f;
    bool humans_use_llm = true;
    bool animals_use_llm = false;

    template<class Archive>
    void serialize(Archive& ar) { /* cereal fields */ }
};
```

---

## 12. Migration Path

### 12.1 Preserving Existing Behavior

The existing score-based system is **not removed**. It becomes the fallback:

| Component | Before | After |
|-----------|--------|-------|
| `AIC` | `action`, `score` | `action`, `score`, `action_queue`, `waiting_for_llm`, `use_llm` |
| `AISys::tick()` | Always calls `decide()` | Calls `decide()` only when LLM unavailable or `use_llm == false` |
| `AISys::decide()` | Unchanged | Unchanged — still scores WanderBehavior and EatBehavior |
| `WanderBehavior` | Unchanged | Unchanged — still used as fallback |
| `EatBehavior` | Unchanged | Unchanged — still used as fallback |

### 12.2 Migrating Existing Actions

**WanderAction**:
- Inherit `ActionBase<WanderAction, ActionID::Wander>` instead of `Action`
- Add `action_name()` → `"Wander"`
- Add `action_description()` → `"Walk to a random nearby location"`
- Add `get_prerequisites()` → empty vector
- Add `describe()` → `"Wandering around"`
- `act()` logic unchanged

**EatAction**:
- Inherit `ActionBase<EatAction, ActionID::Eat>`
- Add prerequisites: `is_satisfied` checks `Storage::amount(Item::berry) > 0`, resolves to `Harvest`
- **Remove `deploy()` method** — prerequisite chaining handled by `PrerequisiteResolver`
- `act()` logic mostly unchanged (remove the manual HarvestAction creation)

**HarvestAction**:
- Inherit `ActionBase<HarvestAction, ActionID::Harvest>`
- No prerequisites (finding a plant is internal to the action)
- `act()` logic unchanged

### 12.3 Migration Steps

1. Add `ActionID`, `ActionDescriptor`, `ActionRegistry`, `ActionBase` — pure additions, no changes to existing code.
2. Migrate WanderAction, EatAction, HarvestAction to use `ActionBase` — changes are small (add static methods, change base class).
3. Create `PrerequisiteResolver` — new code, used only by LLM path.
4. Add `LLMManager` and providers — new code, registered in game.cpp.
5. Modify `AISys::tick()` — the only potentially breaking change. Test fallback mode first.
6. Add `SpeechBubbleSys` — pure addition.

At each step, the game remains fully functional. Step 5 is the critical integration point
and should be done last, after all infrastructure is in place and tested.

---

## 13. File Structure

```
src/ai/
├── aic.h                              # MODIFIED: add action_queue, waiting_for_llm, use_llm
├── ai.h                               # MODIFIED: add LLM polling logic
├── ai.cpp                             # MODIFIED: dual-mode tick()
│
├── llm/
│   ├── llm_provider.h                 # Abstract provider interface + request/response structs
│   ├── ollama_provider.h              # Ollama implementation
│   ├── ollama_provider.cpp
│   ├── lmstudio_provider.h            # LM Studio implementation
│   ├── lmstudio_provider.cpp
│   ├── claude_provider.h              # Claude API implementation
│   ├── claude_provider.cpp
│   ├── llm_manager.h                  # Central orchestrator
│   ├── llm_manager.cpp
│   ├── prompt_builder.h               # Prompt construction
│   ├── prompt_builder.cpp
│   └── decision_cache.h               # Response caching
│
├── actions/
│   ├── action.h                       # MODIFIED: add ActionID, describe()
│   ├── action_registry.h              # Singleton action type registry
│   ├── action_registry.cpp
│   ├── action_factory.h               # CRTP ActionBase template
│   ├── prerequisite_resolver.h        # Recursive prerequisite chaining
│   ├── prerequisite_resolver.cpp
│   │
│   ├── wander_action.h                # MODIFIED: use ActionBase
│   ├── wander_action.cpp
│   ├── eat_action.h                   # MODIFIED: use ActionBase, remove deploy()
│   ├── eat_action.cpp
│   ├── harvest_action.h               # MODIFIED: use ActionBase
│   ├── harvest_action.cpp
│   │
│   ├── mine_action.h                  # NEW skeleton
│   ├── mine_action.cpp
│   ├── hunt_action.h
│   ├── hunt_action.cpp
│   ├── build_action.h
│   ├── build_action.cpp
│   ├── sleep_action.h
│   ├── sleep_action.cpp
│   ├── trade_action.h
│   ├── trade_action.cpp
│   ├── talk_action.h
│   ├── talk_action.cpp
│   ├── craft_action.h
│   ├── craft_action.cpp
│   ├── fish_action.h
│   ├── fish_action.cpp
│   ├── explore_action.h
│   ├── explore_action.cpp
│   ├── flee_action.h
│   ├── flee_action.cpp
│   ├── store_action.h
│   ├── store_action.cpp
│   ├── retrieve_action.h
│   └── retrieve_action.cpp
│
├── behaviors/                          # UNCHANGED — fallback system
│   ├── behavior.h
│   ├── wander_behavior.h
│   ├── wander_behavior.cpp
│   ├── eat_behavior.h
│   └── eat_behavior.cpp
│
└── speech/
    ├── speech_bubble_sys.h             # NEW: ImGui overlay system
    └── speech_bubble_sys.cpp

src/logic/components/
└── ai_memory.h                         # NEW: entity memory + display state

Build files:
├── vcpkg.json                          # ADD: cpp-httplib
└── CMakeLists.txt                      # ADD: find_package, link, new source files
```

---

## 14. Performance

### 14.1 The Core Problem

With 50 entities using LLM and a 200ms average response time:
- 50 entities × 1 request each = 50 requests
- At 5 RPS: 10 seconds to serve all entities
- By then, the first entities are done and need new decisions

This creates a steady-state queue. The system must be designed so this queue doesn't
grow unboundedly.

### 14.2 Mitigation Strategies

#### Strategy 1: Action Queue (Most Impactful)

The LLM returns 3–5 actions per request, not just 1. If an entity works through
5 actions averaging 10 seconds each, it needs a new decision every 50 seconds.

- 50 entities / 50 seconds = 1 request per second average
- Well within 5 RPS limit.

This is the **single most important optimization**. The prompt instructs the LLM to
return a meaningful plan, not just the next immediate action.

#### Strategy 2: Decision Cache

Discretize entity state into buckets:
- Hunger: low (0-3), medium (4-6), high (7-10)
- Has food: yes/no
- Nearby entity types: bitfield (human, animal, plant, resource, structure)
- Time of day: morning/afternoon/evening/night

This creates ~200 unique cache keys. Cache TTL: 60 seconds.

Expected cache hit rate: 40–70% (many entities in similar situations).

#### Strategy 3: Batching

Accumulate entities needing decisions over a short window (50ms). Send them in a single
prompt:

```
Decide for 4 entities:

Entity 1: Hunger 7.3, has berries, near forest...
Entity 2: Hunger 2.1, no items, near village...
Entity 3: Hunger 5.0, has wood, near river...
Entity 4: Hunger 8.9, no items, in desert...

Respond with a JSON array of 4 decisions.
```

This reduces HTTP overhead and can improve throughput 2–4x. However, larger prompts
increase latency per request. Best for local models where network overhead dominates.

#### Strategy 4: Rate Limiting

Token bucket algorithm:
- Bucket capacity: `rate_limit_rps`
- Refill rate: `rate_limit_rps` tokens per second
- Each request consumes 1 token
- If bucket empty: request waits in queue

Prevents overwhelming the LLM server.

#### Strategy 5: Wander Fallback

Entities **always** have a valid action. While waiting for LLM:
- If current action is null or done → create WanderAction
- WanderAction is interruptible → replaced immediately when LLM result arrives

There is never a frame where an entity is frozen waiting for the LLM.

#### Strategy 6: Priority Queue

Not all entities need LLM decisions equally urgently:
- Entity with critical hunger (> 8) → high priority
- Entity that just finished one action → medium priority
- Entity with 3+ actions still in queue → low priority (skip this cycle)

The `LLMManager` sorts the request queue by priority before processing.

#### Strategy 7: Per-Entity Toggle

```cpp
// Only humans use LLM
if (reg.all_of<Human>(entity))
    ai.use_llm = settings.llm.humans_use_llm;
else
    ai.use_llm = settings.llm.animals_use_llm;
```

Animals use the fast, free score-based fallback. Only the interesting entities (humans)
get LLM-driven behavior. This directly reduces request volume.

### 14.3 Steady-State Analysis

**Conservative scenario** (50 human entities, 200ms latency, 2 concurrent workers):

| Parameter | Value |
|-----------|-------|
| Entities | 50 |
| Actions per LLM call | 4 average |
| Action duration | 8s average |
| Decision interval | 32s per entity |
| Request rate | 50/32 = 1.56 RPS |
| Cache hit rate | 50% |
| Actual request rate | 0.78 RPS |
| Throughput (2 workers, 200ms) | 10 RPS |
| **Queue depth** | **< 1 (empty)** |

The system is comfortably within limits. Even with 200 entities, the queue depth stays
under 5 with these parameters.

**Worst case** (200 entities, 1000ms latency, 1 worker, no cache):

| Parameter | Value |
|-----------|-------|
| Request rate | 200/32 = 6.25 RPS |
| Throughput | 1 RPS |
| Queue growth | 5.25 requests/second |
| Queue after 60s | 315 pending requests |

This is unsustainable. Mitigations: enable caching (brings rate to ~3 RPS), use faster
model, increase workers to 3 (throughput 3 RPS), or reduce entity count using LLM.

### 14.4 Memory Overhead

| Component | Per Entity | 100 Entities |
|-----------|-----------|--------------|
| AIC (expanded) | ~200 bytes | 20 KB |
| AIMemory | ~500 bytes | 50 KB |
| Action queue | ~80 bytes | 8 KB |
| Prompt string (transient) | ~2 KB | — (not stored) |
| Cache (global) | — | ~50 KB |
| **Total** | | **~130 KB** |

Negligible compared to terrain data and GPU buffers.

---

## 15. Problems & Solutions

### Problem 1: LLM Response Latency

**Issue**: 200ms–2000ms per response. Game runs at 16ms/frame.

**Solution**: Fully async architecture. Worker threads handle HTTP calls. Entities wander
while waiting. Results are polled each frame and applied when ready. The player never
notices the delay because entities are always doing something.

### Problem 2: LLM Hallucination

**Issue**: LLM might return action names that don't exist ("Fly", "Teleport"), or
malformed JSON.

**Solution**: Strict validation. Action names are mapped through `ActionRegistry` — unknown
names are silently dropped. If all actions are invalid, default to `[Wander]`. Malformed
JSON also defaults to `[Wander]`. The LLM can never produce invalid game state because
it only selects from pre-defined actions; it doesn't control game logic directly.

### Problem 3: Entity Destroyed During Async Wait

**Issue**: Entity dies between LLM request submission and result delivery.

**Solution**: Result handler checks `reg.valid(result.entity)` before applying. Invalid
results are silently discarded. The `AIC::waiting_for_llm` flag prevents double-submissions.

### Problem 4: Prompt Size Growth

**Issue**: With many nearby entities or complex inventory, prompts could grow large.

**Solution**: Hard limits on serialized data:
- Max 8 nearby entities (sorted by distance, closest first)
- Max 5 recent events
- Inventory summarized (not item-by-item for large inventories)
- Total prompt stays under 600 tokens

### Problem 5: Cost Management (Claude API)

**Issue**: Claude API charges per token. 50 entities × 600 input tokens × 100 output tokens = 35K tokens per decision cycle.

**Solution**:
- Default to local models (ollama/LM Studio) — free.
- Claude API is opt-in, with clear cost display in UI.
- Decision cache reduces redundant calls by 50%+.
- Action queue (4+ actions per call) reduces call frequency by 4x.
- Rate limiting prevents runaway costs.
- Settings allow per-entity LLM toggle to limit which entities use Claude.

### Problem 6: LLM Server Unavailable

**Issue**: Ollama not running, LM Studio closed, API key invalid.

**Solution**: `LLMProvider::is_available()` health check. `LLMManager::is_available()`
returns false if active provider fails health check. `AISys::tick()` falls through to
score-based `decide()` automatically. UI shows connection status. No crash, no error
spam — just graceful degradation.

### Problem 7: Thread Safety

**Issue**: Worker threads access request/result queues concurrently with the game thread.

**Solution**: Use the existing `ThreadSafe<T>` wrapper from `src/util/util.h`. This
provides RAII mutex-locked access. The game thread writes to request_queue and reads from
result_queue. Worker threads read from request_queue and write to result_queue. No shared
mutable state beyond these two queues.

**Important**: Worker threads must NOT access the `entt::registry`. All world state is
serialized into the prompt string on the game thread before submission. The worker thread
only sees strings, not ECS data.

### Problem 8: Action Prerequisite Cycles

**Issue**: Craft needs Iron → Mine needs Pickaxe → Craft needs Iron → ...

**Solution**: Cycle detection via `visited` set in the recursive resolver. If a cycle is
detected, the resolver breaks it and creates the action directly (it will fail, triggering
a new LLM decision). Max depth limit (5) also prevents pathological chains.

**Better long-term fix**: Design prerequisites to avoid cycles. Pickaxe should be
craftable from basic materials (wood + stone, no iron), breaking the cycle.

### Problem 9: Talk Action — Two-Entity Coordination

**Issue**: When Entity A talks to Entity B, both need to be involved. Entity B might be
in the middle of another action.

**Solution**: Talk is modeled as a one-way event:
1. Entity A pathfinds to Entity B.
2. Entity A generates dialogue via LLM.
3. Dialogue is displayed above Entity A.
4. An event ("Entity A said: ...") is added to Entity B's `AIMemory`.
5. Entity B's LLM will see this event in its next decision prompt and may choose to respond.

This is **asynchronous conversation**: entities don't pause to chat in real-time. They
exchange messages through the event system, and their LLMs decide whether/how to respond.
This avoids complex synchronization and feels natural in a simulation.

### Problem 10: Model Quality Variance

**Issue**: Small local models (7B params) produce lower-quality decisions than Claude.

**Solution**: The system prompt is carefully crafted for reliability:
- Strict JSON format requirement
- Explicit list of valid action names
- Simple, constrained output (just action names, not complex reasoning)
- Temperature 0.7 balances creativity/reliability

For very small models, the prompt can be simplified further (remove thought/dialogue,
just request action names). This is configurable per-provider in settings.

### Problem 11: Initial Loading / Cold Start

**Issue**: When the game starts with 50 entities, all entities need decisions simultaneously,
creating a burst of 50 requests.

**Solution**: Stagger initial decisions. In `AISys::init()`, assign each entity a random
delay (0–10 seconds) before first LLM request. During this delay, entities wander.

```cpp
void AISys::init() {
    auto view = reg.view<AIC>();
    float delay = 0;
    view.each([&](auto entity, auto& ai) {
        ai.initial_delay = delay;
        delay += 0.2f;  // 200ms between first requests
    });
}
```

This spreads the initial burst over 10 seconds instead of hitting the LLM with 50
simultaneous requests.

---

## 16. Implementation Phases

### Phase 1: Action System Foundation (No LLM)

- Add `ActionID` enum
- Add `ActionDescriptor`, `ActionPrerequisite` structs
- Create `ActionRegistry` singleton
- Create `ActionBase<>` CRTP template
- Migrate `WanderAction`, `EatAction`, `HarvestAction` to `ActionBase`
- Create `PrerequisiteResolver`
- Create skeleton action classes for all 12 new actions (Mine, Hunt, Build, etc.)
- Verify game still works identically

### Phase 2: LLM Infrastructure

- Add `cpp-httplib` to vcpkg.json and CMakeLists.txt
- Create `LLMProvider` interface, `LLMRequest`/`LLMResponse` structs
- Implement `OllamaProvider`
- Create `LLMManager` with worker threads, queues, rate limiting
- Create `PromptBuilder`
- Create `DecisionCache`
- Create `AIMemory` component
- Test with standalone prompt → response cycle (no game integration yet)

### Phase 3: Game Integration

- Expand `AIC` component with `action_queue`, `waiting_for_llm`, `use_llm`
- Modify `AISys::tick()` with dual-mode logic
- Add `LLMManager` to registry context in `game.cpp`
- Add LLM settings to `Settings` class
- Test: entities receive LLM decisions and execute action queues
- Test: fallback to score-based system when LLM is off

### Phase 4: Additional Providers

- Implement `LMStudioProvider`
- Implement `ClaudeProvider`
- Add provider switching in `LLMManager`
- Test switching between providers at runtime

### Phase 5: UI

- Create `SpeechBubbleSys` (ImGui overlays)
- Add LLM configuration panel to DevMenuSys
- Register `SpeechBubbleSys` in game.cpp
- Test speech bubbles, thought display, action labels

### Phase 6: Action Implementation

- Implement the action logic for each skeleton action (one at a time)
- Add corresponding ECS systems (MineSys, BuildSys, CraftSys, etc.)
- Add new components as needed (Mine, Build, Craft, etc.)
- Expand Item enum, Storage, and Object types
- Test each action individually, then in combination with LLM decisions

---

*Document version: 1.0 — March 2026*
*For the Dynamical simulation engine*
