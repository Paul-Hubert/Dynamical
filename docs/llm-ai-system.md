# LLM-Driven AI System — Design Document

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current System Analysis](#2-current-system-analysis)
3. [Architecture Overview](#3-architecture-overview)
4. [LLM Client Library Selection](#4-llm-client-library-selection)
5. [Action System Redesign](#5-action-system-redesign)
6. [Action Parameters](#6-action-parameters)
7. [Prerequisite Resolution](#7-prerequisite-resolution)
8. [Conversation System — Trade & Talk](#8-conversation-system--trade--talk)
9. [Personality System](#9-personality-system)
10. [Async LLM Integration](#10-async-llm-integration)
11. [Prompt Construction](#11-prompt-construction)
12. [Action Skeleton Reference](#12-action-skeleton-reference)
13. [Speech Bubbles & Action Display](#13-speech-bubbles--action-display)
14. [LLM Configuration UI](#14-llm-configuration-ui)
15. [Migration Path](#15-migration-path)
16. [File Structure](#16-file-structure)
17. [Performance](#17-performance)
18. [Problems & Solutions](#18-problems--solutions)
19. [Implementation Phases](#19-implementation-phases)

---

## 1. Executive Summary

This document describes a redesign of the Dynamical AI system to replace the current
score-based behavior selection with LLM-driven decision making. Instead of hardcoded
scoring functions (e.g. "if hunger > 5, eat"), each AI entity will query a language model
— running locally via **ollama** or **LM Studio**, or remotely via the **Claude API** or
any other OpenAI-compatible endpoint — to decide what actions to take.

The LLM acts as a **planner**, not a **controller**. It receives a snapshot of the
entity's state and returns a prioritized list of action objects with parameters (e.g.
`{"action": "Harvest", "target": "oak_tree"}` or `{"action": "Trade", "offer": {...}}`).
The game engine executes those actions through the existing action/phase state machine,
automatically resolving prerequisites. This keeps the simulation deterministic and robust
— the LLM cannot produce invalid game state, only choose from valid parameterized actions.

Each AI entity has a **randomly generated personality** (traits, motivations, speech style)
that is included in every LLM prompt, creating distinct characters that behave differently
even in identical situations. Personalities can be hand-edited or AI-generated.

**Trade** and **Talk** actions create **conversational states** where two entities exchange
messages back and forth via async LLM calls, without blocking either entity's task
execution.

Key design goals:

- **Unified LLM API**: **ai-sdk-cpp (ClickHouse) is the sole HTTP client library.** All providers (Ollama, LM Studio, Claude, OpenAI, etc.) are accessed through ai-sdk-cpp's consistent interface.
- **Non-blocking**: LLM calls happen on worker threads; entities continue tasks while waiting.
- **Graceful fallback**: When the LLM is unavailable, the existing score-based system runs.
- **Switchable providers**: Change between Ollama, LM Studio, Claude, OpenAI, or any OpenAI-compatible endpoint at runtime — no code changes, just settings.
- **Parameterized actions**: Actions carry typed parameters (target resource, trade offer, message text).
- **Automatic prerequisites**: A recursive resolver chains dependent actions automatically.
- **Conversational AI**: Trade and Talk create async back-and-forth exchanges between entities.
- **Unique personalities**: Each entity has generated traits that shape LLM decisions and speech.
- **Optional batching**: Request batching is configurable via settings (default: disabled for per-personality quality).
- **Infinite inventory**: Resources are tracked narratively in AIMemory; physical storage system removed.
- **Visual feedback**: ImGui speech bubbles show entity thoughts, dialogue, and current action.

---

## 2. Current System Analysis

### 2.1 How AI Decisions Work Today

The current system lives in `src/ai/` and follows a simple pattern:

```
AISys::tick()
  +-- For each entity with AIC component:
       +-- If action == nullptr OR action->interruptible:
       |    +-- decide() -> score all behaviors, pick highest
       +-- Execute: ai.action = ai.action->act(move(ai.action))
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
| No parameterized actions | Actions can't specify targets, offers, or messages |

---

## 3. Architecture Overview

### 3.1 High-Level Data Flow

```
+----------------------------------------------------------+
|                     GAME LOOP (per frame)                 |
|                                                          |
|  +---------+    +----------+    +-----------+           |
|  | Systems |--->|  AISys   |--->|  Renderer |           |
|  | pre_tick|    |  tick()  |    |  render() |           |
|  +---------+    +----+-----+    +-----------+           |
|                      |                                    |
|         +------------+------------+                      |
|         |            |            |                      |
|         v            v            v                      |
|  +------------+ +---------+ +----------+                |
|  | Poll LLM   | | Execute | | Submit   |                |
|  | Results    | | Actions | | Requests |                |
|  +-----+------+ +---------+ +----+-----+                |
|        |                          |                      |
+--------+--------------------------+----------------------+
         |                          |
         |    +------------------+  |
         +--->|   LLMManager     |<-+
              |  (worker threads) |
              |                  |
              |  request_queue --+--> Worker Thread 1 --> LLMClient --> LLM
              |  result_queue  <-+--  Worker Thread 2 --> LLMClient --> LLM
              |  cache           |
              +------------------+
                       |
              +--------+--------+-------- - - -
              v        v        v
          +-------+ +-------+ +--------+
          |Ollama | |LM     | |Claude  |  ... any OpenAI-compatible
          |:11434 | |Studio | |API     |
          +-------+ |:1234  | +--------+
                    +-------+
```

### 3.2 Decision Flow for a Single Entity

```
1. AISys::tick() finds entity with empty action queue and no pending LLM request
2. PromptBuilder serializes entity state + personality -> prompt string
3. LLMManager::submit() enqueues the request (non-blocking)
4. Entity continues current task or wanders while waiting
5. Worker thread dequeues request, calls LLMClient (unified library)
6. LLM returns JSON:
   {
     "thought": "I'm hungry and there's an oak tree nearby...",
     "actions": [
       {"action": "Harvest", "target": "oak_tree"},
       {"action": "Eat", "food": "acorn"},
       {"action": "Talk", "target": "Entity#42", "message": "Hey, got any berries?"}
     ],
     "dialogue": "Time to get some food!"
   }
7. LLMManager parses response -> LLMDecisionResult with parameterized action queue
8. Next frame: AISys polls results, sets AIC::action_queue
9. Pop Harvest{target=oak_tree} -> PrerequisiteResolver checks prereqs -> chain built
10. Entity executes action phases
11. Queue empty -> read AIMemory (past conversations, events) -> submit new LLM request
```

### 3.3 Dual-Mode Decision Logic

The modified `AISys::tick()` runs in two modes:

**LLM Mode** (when `AIC::use_llm == true` and LLM is available):
- Actions come from the LLM-assigned `action_queue` with parameters
- Prerequisites are resolved automatically by `PrerequisiteResolver`
- When queue is empty, AIMemory (including unread conversations) is included in the next LLM prompt
- Entity continues current action or wanders while waiting for LLM response

**Fallback Mode** (when LLM is unavailable or `use_llm == false`):
- Runs the existing `decide()` method unchanged
- Score-based behavior selection: WanderBehavior, EatBehavior, etc.
- Zero LLM dependency — the game works identically to today

---

## 4. LLM Client Library: ai-sdk-cpp

### 4.1 Why a Unified Library

Writing manual HTTP request/response code for each LLM provider (ollama, LM Studio,
Claude, OpenAI, etc.) creates significant boilerplate: different JSON formats, different
auth headers, different streaming protocols, different error handling.

**ai-sdk-cpp (ClickHouse) is the sole LLM client library for this project.** It provides:

- **Unified API** across OpenAI, Anthropic, and any OpenAI-compatible endpoint (ollama, LM Studio, OpenRouter, etc.)
- **C++20** — matches the Dynamical project standard
- **Streaming support** — can show token-by-token generation in speech bubbles
- **Tool/function calling** — enables structured JSON responses via native tool use
- **Apache 2.0** — permissive license

### 4.2 Integration

**Setup as git submodule:**

```bash
git submodule add https://github.com/ClickHouse/ai-sdk-cpp lib/ai-sdk-cpp
```

**CMakeLists.txt:**

```cmake
add_subdirectory(lib/ai-sdk-cpp)
target_link_libraries(dynamical PRIVATE ai-sdk-cpp)
```

**Note on dependencies**: ai-sdk-cpp bundles its own patched version of `nlohmann_json` (with thread-safety fixes) and handles HTTP/TLS internally. No additional vcpkg dependencies are required beyond what you already have (C++20 compiler, CMake 3.16+).

### 4.3 LLMClient Wrapper

Create a thin wrapper (`src/llm/llm_client.h`) that handles game-specific logic:
- Abstracts provider selection (read from config)
- Handles request/response batching (optional, flag-controlled)
- Converts game-specific structures to/from LLM JSON
- Caches decision results for personality + state combinations
- Manages request queuing and rate limiting

```cpp
class LLMClient {
public:
    // Configure active provider and model at startup
    void configure(const std::string& provider, const std::string& api_key);

    // Single request (immediate)
    LLMResponse request(const LLMPrompt& prompt);

    // Batched requests (optional, if batching_enabled in config)
    void enqueue_request(const LLMPrompt& prompt, RequestCallback callback);
    void flush_batch();  // Send all queued requests

private:
    std::unique_ptr<ai_sdk_cpp::Client> client;
    std::vector<PendingRequest> batch_queue;
    bool batching_enabled = false;
};
```

**No other HTTP client libraries are used.** All LLM communication goes through ai-sdk-cpp. The library natively supports:
- OpenAI-compatible endpoints (Ollama, LM Studio, OpenRouter, etc.)
- Anthropic Claude API with full native support (including Extended Thinking, Prompt Caching)
- Multi-provider tool/function calling and streaming

### 4.4 Provider Configuration

Using ai-sdk-cpp, switching providers is a URL + key change:

```cpp
// Ollama (local, no key)
auto client = ai::Client("http://localhost:11434/v1", "");

// LM Studio (local, no key)
auto client = ai::Client("http://localhost:1234/v1", "");

// Claude API (remote, key required)
auto client = ai::Client("https://api.anthropic.com", api_key, ai::Provider::Anthropic);

// OpenRouter, Together.ai, or any other endpoint
auto client = ai::Client("https://openrouter.ai/api/v1", api_key);
```

Adding a new provider = entering a new URL in the settings UI. No code changes.

### 4.5 Integration Checklist

- ✅ Add ai-sdk-cpp as git submodule (see section 4.2)
- ✅ Update CMakeLists.txt to include and link ai-sdk-cpp
- ✅ No new vcpkg dependencies required (ai-sdk-cpp is self-contained)
- ✅ Ensure C++20 compiler support
- ✅ Set `ANTHROPIC_API_KEY` environment variable for Claude provider (optional; Ollama/LM Studio need no key)

---

## 5. Action System Redesign

### 5.1 ActionID Enum

A central enum identifying every action type in the game:

```cpp
enum class ActionID : int {
    None = 0,
    Wander, Eat, Harvest, Mine, Hunt, Build, Sleep,
    Trade, Talk, Craft, Fish, Explore, Flee
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
    std::string name;           // "Eat" -- matches LLM output
    std::string description;    // "Consume food to reduce hunger"
    std::vector<ActionPrerequisite> prerequisites;
    std::function<std::unique_ptr<Action>(entt::registry&, entt::entity, const ActionParams&)> create;
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
with a `BasicNeeds` component can Eat; only entities near water can Fish.

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
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<Derived>(r, e, p);
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
7. Constructor accepting `const ActionParams&` for LLM-specified parameters

No manual registration, no modification of `ai.cpp`, no changes to any other file.

### 5.5 Modified Action Base Class

The existing `Action` class gains virtual methods and params:

```cpp
class Action {
public:
    Action(entt::registry& reg, entt::entity entity, const ActionParams& params = {})
        : reg(reg), entity(entity), params(params) {}

    virtual ActionID id() const = 0;
    virtual std::string describe() const = 0;
    virtual std::unique_ptr<Action> act(std::unique_ptr<Action> self) = 0;
    virtual ~Action() {}

    void link(Action* before, std::unique_ptr<Action> after);
    std::unique_ptr<Action> nextAction();

    bool interruptible = false;
    entt::registry& reg;
    entt::entity entity;
    ActionParams params;    // LLM-specified parameters for this action instance
private:
    std::unique_ptr<Action> next = nullptr;
};
```

---

## 6. Action Parameters

### 6.1 Why Actions Need Parameters

The LLM doesn't just say "Harvest" — it says "Harvest the oak tree" or "Trade 5 berries
for 2 wood with Entity#42". Each action instance carries parameters that specify *what*
to act on, *who* to interact with, and *how*.

### 6.2 ActionParams Structure

```cpp
struct TradeOffer {
    std::string give_item;       // e.g. "berry"
    int give_amount;             // e.g. 5
    std::string want_item;       // e.g. "wood"
    int want_amount;             // e.g. 2
};

struct ActionParams {
    // Target specification
    std::string target_type;     // "oak_tree", "stone_deposit", "berry_bush", etc.
    entt::entity target_entity = entt::null;  // Resolved from target_type or target_name

    // Entity interaction
    std::string target_name;     // "Entity#42" for Talk/Trade
    std::string message;         // Text for Talk action
    TradeOffer trade_offer;      // For Trade action

    // Resource specification
    std::string resource;        // "wood", "stone", "berry" — what to gather/craft/use
    int amount = 1;              // How much

    // Crafting
    std::string recipe;          // "wooden_sword", "stone_pickaxe", etc.

    // Building
    std::string structure;       // "house", "wall", "storage", etc.

    // Generic
    std::string direction;       // "north", "south", etc. for Explore/Flee
    float duration = 0;          // How long (seconds) for Sleep, etc.
};
```

### 6.3 Parameters Per Action

| Action | Required Parameters | Optional Parameters | Example LLM Output |
|--------|--------------------|--------------------|-------------------|
| **Wander** | — | `direction` | `{"action": "Wander"}` |
| **Eat** | — | `food` (item type) | `{"action": "Eat", "food": "berry"}` |
| **Harvest** | — | `target_type` (plant/tree) | `{"action": "Harvest", "target": "berry_bush"}` |
| **Mine** | — | `target_type` (deposit), `resource` | `{"action": "Mine", "target": "stone_deposit"}` |
| **Hunt** | — | `target_type` (animal) | `{"action": "Hunt", "target": "bunny"}` |
| **Build** | `structure` | — | `{"action": "Build", "structure": "house"}` |
| **Sleep** | — | `duration` | `{"action": "Sleep"}` |
| **Trade** | `target_name`, `trade_offer` | — | `{"action": "Trade", "target": "Entity#42", "offer": {"give": "berry x5", "want": "wood x2"}}` |
| **Talk** | `target_name`, `message` | — | `{"action": "Talk", "target": "Entity#42", "message": "Hello! Nice weather."}` |
| **Craft** | `recipe` | — | `{"action": "Craft", "recipe": "wooden_sword"}` |
| **Fish** | — | — | `{"action": "Fish"}` |
| **Explore** | — | `direction` | `{"action": "Explore", "direction": "north"}` |
| **Flee** | — | `direction` | `{"action": "Flee", "direction": "south"}` |

### 6.4 Parameter Resolution

When the LLM specifies a target by name (e.g. `"target": "berry_bush"`), the action's
initialization phase resolves it to an actual entity:

```
Phase 0 of HarvestAction:
  1. Read params.target_type ("berry_bush")
  2. Query MapManager for nearest Object with identifier matching target_type
  3. If found: set params.target_entity, pathfind to it
  4. If not found: action fails -> triggers new LLM decision with updated context
```

For entity targets (Trade, Talk), resolution uses the entity's name/ID:

```
Phase 0 of TalkAction:
  1. Read params.target_name ("Entity#42")
  2. Lookup entity by name in registry
  3. If found and within range: pathfind to it
  4. If not found: action fails
```

### 6.5 Invalid Parameters

If the LLM provides invalid parameters (nonexistent target, impossible recipe), the
action fails gracefully in Phase 0. The failure is recorded in `AIMemory` and the next
LLM prompt includes context like: "Failed: Harvest — no berry_bush nearby". The LLM
adapts its next decision accordingly.

---

## 7. Prerequisite Resolution

### 7.1 The Problem

Currently, `EatAction::deploy()` manually creates a `HarvestAction` and chains itself
after it. This means:
- Every action must know its own prerequisites.
- Adding a new prerequisite means modifying the action's code.
- Complex chains (Craft -> need Wood -> Harvest tree OR Trade for Wood) require complex
  conditional logic inside each action.

### 7.2 The Recursive Resolver

The `PrerequisiteResolver` externalizes this logic:

```cpp
class PrerequisiteResolver {
public:
    std::unique_ptr<Action> resolve(
        ActionID target,
        entt::registry& reg,
        entt::entity entity,
        const ActionParams& params,
        int max_depth = 5
    );
};
```

**Algorithm:**

```
resolve(target, entity, params, depth=0, visited={}):
    if depth > max_depth: return create(target, params)  // safety limit
    if target in visited: return create(target, params)   // cycle break

    visited.add(target)
    descriptor = registry.get(target)
    chain_head = nullptr

    for each prerequisite in descriptor.prerequisites:
        if not prerequisite.is_satisfied(reg, entity):
            prereq_id = prerequisite.resolve_action(reg, entity)
            prereq_chain = resolve(prereq_id, entity, {}, depth+1, visited)
            append prereq_chain to chain

    target_action = descriptor.create(reg, entity, params)
    append target_action to chain
    return chain_head (or target_action if no prerequisites needed)
```

### 7.3 Example: Eating When Hungry

```
resolve(Eat, params={food:"berry"})
  +-- Check prerequisite: "has food in inventory"
  |   +-- NOT satisfied (no berries)
  |   +-- resolve_action returns: Harvest
  |       +-- resolve(Harvest, params={target:"berry_bush"})
  |           +-- Check prerequisite: "nearby harvestable plant"
  |           |   +-- IS satisfied (berry bush nearby)
  |           +-- Create HarvestAction{target=berry_bush}
  +-- Create EatAction{food=berry}
  +-- Chain: HarvestAction -> EatAction
```

### 7.4 Example: Crafting a Wooden Sword

```
resolve(Craft, params={recipe:"wooden_sword"})
  +-- Check: "has required materials" (needs 3 wood + 1 stone)
  |   +-- NOT satisfied (need wood)
  |   +-- resolve_action: nearby trees? -> Harvest
  |       +-- resolve(Harvest, params={target:"tree"})
  |           +-- Create HarvestAction{target=tree}
  +-- Check: "has required materials" (needs stone)
  |   +-- NOT satisfied (need stone)
  |   +-- resolve_action: stone deposit nearby? -> Mine
  |       +-- resolve(Mine, params={target:"stone_deposit"})
  |           +-- Create MineAction{target=stone_deposit}
  +-- Create CraftAction{recipe=wooden_sword}
  +-- Chain: HarvestAction -> MineAction -> CraftAction
```

### 7.5 Cycle Detection and Depth Limiting

The `visited` set prevents cycles like: Trade->needs gold->Mine->needs pickaxe->Craft->needs iron->Trade...

The `max_depth` limit (default 5) prevents pathological chains. If depth is exceeded, the
target action is created directly — it will likely fail, and the failure triggers a new LLM
decision with updated context ("Craft failed: missing materials").

### 7.6 Dynamic Resolution

Prerequisites use `std::function`, allowing runtime decisions:

```cpp
// Craft's prerequisite for wood:
ActionPrerequisite{
    .is_satisfied = [](auto& reg, auto e) {
        // With infinite inventory, prerequisites are now context-based
        // (e.g., can entity pathfind to a wood source?)
        return true;  // Resources are unlimited; check availability instead
    },
    .resolve_action = [](auto& reg, auto e) {
        // Check if there's a tree nearby -> Harvest
        // Otherwise -> Trade
        if (findNearest(reg, e, Object::tree))
            return ActionID::Harvest;
        else
            return ActionID::Trade;
    },
    .description = "needs access to wood source"
}
```

**Note on Inventory**: Entities now have **infinite inventory**. Prerequisites focus on
availability and accessibility (e.g., "is there a tree nearby?") rather than "do I have
enough items?". This simplifies the action system and allows focus on decision-making and
conversation rather than resource tracking.

---

## 8. Conversation System — Trade & Talk

### 8.1 Design Philosophy

Trade and Talk are fundamentally different from other actions. They involve **two entities**
exchanging messages over multiple turns. Neither entity blocks — both continue their tasks
while conversation happens asynchronously through LLM requests triggered by incoming
messages.

### 8.2 Conversation State

```cpp
struct Conversation {
    entt::entity partner;
    enum class Type { Talk, Trade } type;
    enum class State { Pending, Active, Concluded } state;
    std::vector<ConversationMessage> messages;
    double started;          // game time
    double last_message;     // game time

    // Trade-specific
    TradeOffer current_offer;    // Latest offer on the table
    bool offer_accepted = false;
};

struct ConversationMessage {
    entt::entity speaker;
    std::string text;
    double timestamp;
};
```

### 8.3 Talk Flow (Async Back-and-Forth)

```
Entity A decides to Talk to Entity B (LLM chose: {"action": "Talk", "target": "B", "message": "Hello!"})

1. TalkAction Phase 0: Pathfind A to B.
2. TalkAction Phase 1: Arrive. Create Conversation{A, B, Talk, Active}.
3. TalkAction Phase 2: Add message "Hello!" to Conversation.
   - Set A's AIMemory.current_dialogue = "Hello!"
   - Add event to B's AIMemory: "Entity A said: 'Hello!'"
   - TalkAction for A completes. A moves to next action in queue.
4. B continues its current task normally.
5. When B's action queue empties (or B's next LLM decision happens):
   - B's prompt includes: "Recent messages: Entity A said 'Hello!' to you"
   - B's AIMemory has unread conversation entries
   - LLM may respond: {"action": "Talk", "target": "A", "message": "Hey! Got any berries?"}
6. B's TalkAction executes, adds message to the Conversation.
   - Add event to A's AIMemory: "Entity B said: 'Hey! Got any berries?'"
7. Cycle continues until either entity stops choosing Talk.

Conversation concludes when:
  - Neither entity responds for 30 game-seconds
  - Either entity moves too far away
  - Either entity's LLM doesn't pick Talk in response
```

**Key principle**: Neither entity waits for the other to respond. A says something and
moves on. When B finishes its current task, its LLM sees the message and may choose to
respond. This creates natural, asynchronous conversation pacing.

### 8.4 Trade Flow (Offer/Counter/Accept/Reject)

```
Entity A decides to Trade with Entity B:
  {"action": "Trade", "target": "B", "offer": {"give": "berry x5", "want": "wood x2"}}

1. TradeAction Phase 0: Pathfind A to B.
2. TradeAction Phase 1: Arrive. Create Conversation{A, B, Trade, Pending}.
   - Set current_offer = {berry x5 -> wood x2}
   - Add message: "I'll give you 5 berries for 2 wood"
   - Add event to B's AIMemory: "Entity A offers: 5 berries for 2 wood"
3. TradeAction for A completes. A moves to next action.
4. When B's LLM runs next (sees the trade offer in prompt):
   Option a) Accept:
     {"action": "Trade", "target": "A", "offer": {"accept": true}}
     -> Trade is recorded in both entities' AIMemory. Both get event: "Trade completed with Entity A."
        (With infinite inventory, trades are conceptual exchanges recorded in dialogue, not item transfers)
   Option b) Counter-offer:
     {"action": "Trade", "target": "A", "offer": {"give": "wood x1", "want": "berry x3"}}
     -> Updated offer, A sees counter-offer in next prompt.
   Option c) Reject (or just don't choose Trade):
     -> Conversation times out. Both get event: "Trade with Entity X fell through."
```

### 8.5 Incoming Message Handling

When an entity has unread messages in AIMemory, two things happen:

1. **Immediate**: A `MessageReceived` flag is set on the entity. This does NOT interrupt
   the current action — the entity finishes what it's doing.
2. **At next decision point**: When the entity needs a new LLM decision (action queue
   empty or current action completes), the prompt includes all unread messages. The LLM
   naturally sees them and can choose to respond via Talk or Trade.

For **urgent** messages (trade offers about to expire), the `MessageReceived` event can
trigger an early LLM request even if the action queue isn't empty (configurable).

### 8.6 LLM Prompt for Conversations

When an entity has active conversations, the prompt includes:

```
=== ACTIVE CONVERSATIONS ===

Conversation with Entity#42 (Talk, started 15s ago):
  Entity#42: "Hello! Nice weather today."
  You: "Indeed! Have you found any good berry bushes?"
  Entity#42: "There's a great one to the north!"
  [You haven't responded yet]

Trade offer from Entity#17 (pending 8s):
  Entity#17 offers: 5 berries for 2 of your wood
  [Accept, counter-offer, or ignore]
```

This gives the LLM full conversation context to generate appropriate responses.

---

## 9. Personality System

### 9.1 Design

Each AI entity has a `Personality` component that shapes its behavior. The personality is
included in the LLM system prompt, so the same game state produces different decisions
for different entities.

### 9.2 Personality Component

```cpp
struct Personality {
    // Core traits (0.0 to 1.0 scale)
    float friendliness;     // 0=hostile, 1=warm and social
    float industriousness;  // 0=lazy, 1=workaholic
    float curiosity;        // 0=stays home, 1=explorer
    float caution;          // 0=reckless, 1=very careful
    float generosity;       // 0=greedy, 1=gives freely
    float talkativeness;    // 0=quiet, 1=chatterbox

    // Identity
    std::string name;           // "Elara", "Grog", etc.
    std::string speech_style;   // "formal", "casual", "grumpy", "poetic"
    std::string backstory;      // 1-2 sentences

    // Preferences
    std::string favorite_activity;  // "mining", "exploring", etc.
    std::string dislikes;           // "cold weather", "crowds", etc.

    // Full personality prompt (can be hand-edited or AI-generated)
    std::string custom_prompt;  // Overrides trait-based prompt if non-empty
};
```

### 9.3 Random Generation

When a new entity is spawned, its personality is randomly generated:

```cpp
Personality generate_personality(std::mt19937& rng) {
    Personality p;
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    p.friendliness = dist(rng);
    p.industriousness = dist(rng);
    p.curiosity = dist(rng);
    p.caution = dist(rng);
    p.generosity = dist(rng);
    p.talkativeness = dist(rng);

    // Name from a pool
    p.name = random_name(rng);

    // Speech style based on traits
    if (p.friendliness > 0.7f) p.speech_style = "warm and friendly";
    else if (p.friendliness < 0.3f) p.speech_style = "curt and cold";
    else p.speech_style = "neutral";

    // Backstory placeholder
    p.backstory = "";

    return p;
}
```

### 9.4 AI-Generated Personalities

Optionally, the personality's `backstory` and `custom_prompt` can be generated by the LLM
itself when the entity spawns:

```
Generate a personality for a villager in a medieval simulation.
Traits: friendliness=0.8, industriousness=0.3, curiosity=0.9, caution=0.2
Name: Elara

Write a 2-sentence backstory and a speech style description.
```

This creates richer characters than pure random generation. The cost is one extra LLM call
per entity spawn (can be done at startup or lazily on first decision).

### 9.5 Personality in Prompts

The personality is injected into the system prompt:

```
You are Elara, a curious and friendly villager who loves exploring but isn't very
hardworking. You speak in a warm, enthusiastic way. You're cautious about nothing and
always up for adventure. You dislike staying in one place too long.

Backstory: Elara grew up at the edge of the village and always dreamed of what lay
beyond the forest. She's known for bringing back strange trinkets from her wanderings.
```

This means two entities looking at the same berry bush will make different decisions:
- Elara (curiosity=0.9): "I wonder what's past those trees... I'll explore first."
- Grog (industriousness=0.9): "Time to harvest every bush in sight."

### 9.6 Personality Editing

Personalities are serializable via cereal. A debug ImGui panel allows editing any entity's
personality traits, name, speech style, and custom prompt in real time. Changes take effect
on the next LLM call.

---

## 9.7 Inventory System — Infinite Resources

### 9.7.1 Design Philosophy

Entities have **infinite inventory**. This simplification removes the complexity of storage
management and allows the LLM and action system to focus on **decision-making and conversation**
rather than resource tracking.

### 9.7.2 Why Infinite Inventory?

**Removed**: The `Storage` component, inventory limits, and item tracking in entity state.

**Benefits**:
- **Simpler prerequisites**: Actions check "is a resource nearby?" not "do I have enough?"
- **Focus on choice**: LLM decisions are about what to do, not what can fit
- **Cleaner conversations**: Trade and Talk focus on **conceptual exchanges** (narrative) rather than item movement
- **Easier to extend**: Adding new actions doesn't require storage math
- **Reduces boilerplate**: No need to subtract items, check capacity, etc.

### 9.7.3 How It Works

When an entity executes `Harvest`, `Mine`, or `Hunt`:
- The action completes normally (emit component, wait for duration, resolve)
- Instead of adding to `Storage`, it records the action in `AIMemory`
  ```cpp
  AIMemory event: {timestamp, action_name: "Harvest oak_tree", quantity: 3}
  ```

When the LLM is queried next, it includes this in the history context:
```
Recent actions:
- 5s ago: Harvested 3 berries from berry bush
- 12s ago: Hunted and killed 1 rabbit
- 30s ago: Mined 5 stone from deposit
```

The LLM naturally understands the entity has access to these resources and can plan around them.

### 9.7.4 Trade and Conversation

When two entities trade, it's now a **dialogue-based exchange**:

```
Entity A: "I'll give you 5 berries for 2 wood"
Entity B: "Deal! I've got plenty of wood."
AIMemory records:
  - A: Conceptually gave 5 berries
  - B: Conceptually received 5 berries
```

No physical items move. The **narrative** of the trade is recorded. Future LLM prompts include
this: "You traded 5 berries to Entity B and received 2 wood" — providing context for the entity's
next decision.

### 9.7.5 Migration Notes

**Existing storage checks in actions** should be removed:
- Delete `Storage` component references
- Replace `reg.get<Storage>(e).amount(item)` checks with "is nearby source available?"
- Update prerequisites to check **accessibility** not **possession**

**Example change**:
```cpp
// OLD:
ActionPrerequisite{
    .is_satisfied = [](auto& reg, auto e) {
        return reg.get<Storage>(e).amount(Item::wood) >= 2;
    }
}

// NEW:
ActionPrerequisite{
    .is_satisfied = [](auto& reg, auto e) {
        return findNearby(reg, e, Object::tree) != entt::null;
    }
}
```

---

## 10. Async LLM Integration

### 10.1 Why Async Is Essential

LLM inference takes 200ms-2000ms+ depending on model size and hardware. The game loop
runs at 60fps (16ms per frame). A synchronous LLM call would freeze the game for
12-125 frames. This is unacceptable.

### 10.2 LLMManager Architecture

```cpp
class LLMManager {
    // Unified LLM client using ai-sdk-cpp
    // All providers (Ollama, LM Studio, Claude, OpenAI, etc.) configured identically
    struct ProviderConfig {
        std::string name;           // "Ollama", "Claude", "OpenAI", etc.
        std::string base_url;       // "http://localhost:11434" or Claude's URL
        std::string api_key;        // Optional for local models
        std::string model;          // "llama2", "claude-3-sonnet", "gpt-4", etc.
    };
    ProviderConfig active_provider;
    std::unique_ptr<LLMClient> llm_client;  // Wrapper around ai-sdk-cpp

    // Thread-safe queues (using existing ThreadSafe<T> from src/util/util.h)
    ThreadSafe<std::deque<LLMDecisionRequest>> request_queue;
    ThreadSafe<std::vector<LLMDecisionResult>> result_queue;

    // Worker thread pool
    std::vector<std::thread> workers;
    std::atomic<bool> running{true};
    std::condition_variable work_available;

    // Rate limiting
    int rate_limit_rps = 5;
    int max_concurrent = 2;
    std::atomic<int> active_requests{0};

    // Cache
    DecisionCache cache;

    // Batching (optional, flag-controlled)
    bool batching_enabled = false;
    int batch_size = 4;
    int batch_window_ms = 50;
};
```

**All requests use ai-sdk-cpp.** No manual HTTP calls. Provider switching is transparent to the rest of the codebase.

### 10.3 Request Lifecycle

```
Frame N:   AISys detects entity needs decision
           -> PromptBuilder constructs prompt (includes personality, memory, conversations)
           -> LLMManager::submit(request)         [enqueues, returns immediately]
           -> Entity continues current action or wanders

Frame N+1: Worker thread dequeues request
           -> Checks cache (hit? return cached result)
           -> Checks rate limit (exceeded? wait or skip)
           -> Calls LLMClient (via ai-sdk-cpp) with active provider + model
              ai_sdk_cpp::Client client(...);
              auto response = client.createChatCompletion(messages);
           -> Parses response JSON -> LLMDecisionResult with parameterized actions
           -> Enqueues result in result_queue

Frame N+K: AISys calls LLMManager::poll_results()
           -> Dequeues all completed results
           -> Matches results to entities (validates entity still exists)
           -> Sets AIC::action_queue
           -> Entity starts executing LLM-chosen actions
```

**Implementation detail**: The `LLMClient::request()` method wraps ai-sdk-cpp's `createChatCompletion()` call, handling both streaming and non-streaming modes.

### 10.4 Entity Validity After Async Delay

Between submission and result delivery, an entity might be destroyed. The handler checks:

```cpp
auto results = llm.poll_results();
for (auto& result : results) {
    if (!reg.valid(result.entity)) continue;          // Entity destroyed
    if (!reg.all_of<AIC>(result.entity)) continue;    // AIC removed
    auto& ai = reg.get<AIC>(result.entity);
    if (!ai.waiting_for_llm) continue;                // Stale result
    // ... apply result ...
}
```

### 10.5 Optional Batching

When `batching_enabled == true`, the worker thread accumulates requests for
`batch_window_ms` before sending them as a single batched prompt via ai-sdk-cpp:

```
Decide for 4 entities. Respond with a JSON array:

Entity 1 "Elara" (curious explorer): Hunger 2.1, has berries, near forest...
Entity 2 "Grog" (industrious miner): Hunger 7.3, has stone, near quarry...
Entity 3 "Mira" (friendly trader): Hunger 4.0, has wood, near village...
Entity 4 "Kael" (cautious hunter): Hunger 6.5, no items, near plains...

[{"entity": 1, "thought": "...", "actions": [...]}, ...]
```

When `batching_enabled == false` (default), each entity gets its own individual LLM call.
Batching is configurable via the UI and settings.

**When to enable batching**: Many entities (20+), local model with fast inference, larger
context window. When to keep it off: Fewer entities, cloud API (where per-request is
cleaner), or when individual personality prompts are important.

---

## 11. Prompt Construction

### 11.1 System Prompt (Per-Entity, Includes Personality)

```
You are {name}, a villager in a medieval simulation.

PERSONALITY:
{personality_prompt}
Speak in a {speech_style} manner.

RULES:
- Respond ONLY with a JSON object. No other text.
- Choose from the listed available actions only.
- Actions can have parameters (target, message, offer, etc.).
- "thought" is your brief internal reasoning (shown as thought bubble).
- "dialogue" is optional speech (shown as speech bubble).
- Prerequisites are handled automatically. Say "Eat" not "Harvest then Eat".

RESPONSE FORMAT:
{
  "thought": "Brief reasoning",
  "actions": [
    {"action": "ActionName", ...params...},
    {"action": "ActionName2", ...params...}
  ],
  "dialogue": "Optional speech"
}
```

### 11.2 Entity State Prompt (Dynamic)

```
=== YOUR STATE ===
Name: Elara
Position: (45.2, 23.1) in a grassland area
Hunger: 7.3/10 (getting hungry)
Inventory: berry x2, wood x1
Current activity: Idle

=== NEARBY ENTITIES ===
- Berry Bush at (44.0, 25.0) -- 2.3 tiles away, harvestable
- Grog (Human) at (46.0, 22.0) -- 1.2 tiles away, mining
- Stone deposit at (48.0, 20.0) -- 4.2 tiles away
- Bunny at (42.0, 26.0) -- 4.0 tiles away

=== ACTIVE CONVERSATIONS ===
Trade offer from Grog (pending 8s):
  Grog offers: 3 stone for 2 of your wood
  [Accept, counter-offer, or ignore]

=== RECENT EVENTS ===
- Completed: Harvest (gathered berries from bush) -- 30s ago
- Grog said to you: "Hey Elara, want to trade some stone?" -- 20s ago
- Failed: Hunt (bunny escaped) -- 2min ago

=== AVAILABLE ACTIONS ===
- Wander: Walk to a random nearby location
- Eat [food]: Consume food to reduce hunger. Example: {"action":"Eat","food":"berry"}
- Harvest [target]: Gather from plants/trees. Example: {"action":"Harvest","target":"berry_bush"}
- Mine [target]: Extract stone/ore. Example: {"action":"Mine","target":"stone_deposit"}
- Hunt [target]: Chase animals. Example: {"action":"Hunt","target":"bunny"}
- Build [structure]: Construct. Example: {"action":"Build","structure":"house"}
- Sleep: Rest to restore energy
- Trade [target, offer]: Exchange items. Example: {"action":"Trade","target":"Grog","offer":{"give":"berry x5","want":"wood x2"}}
- Talk [target, message]: Speak to someone. Example: {"action":"Talk","target":"Grog","message":"Hello!"}
- Craft [recipe]: Create items. Example: {"action":"Craft","recipe":"wooden_sword"}
- Fish: Catch fish from water
- Explore [direction]: Scout territory. Example: {"action":"Explore","direction":"north"}
- Flee [direction]: Run from danger

What should Elara do?
```

### 11.3 Prompt Size Management

| Component | Approximate tokens |
|-----------|--------------------|
| System prompt + personality | ~200 |
| Entity state | ~60 |
| Nearby entities (up to 8) | ~120 |
| Active conversations (up to 3) | ~150 |
| Recent events (up to 5) | ~80 |
| Available actions with examples | ~300 |
| **Total** | **~910 tokens** |

Well within limits for all providers.

### 11.4 Response Parsing

```cpp
LLMDecisionResult parse_response(entt::entity entity, const std::string& content) {
    LLMDecisionResult result;
    result.entity = entity;

    // 1. Parse JSON (try/catch for malformed responses)
    auto json = parse_json(content);

    // 2. Extract thought and dialogue
    result.thought = json.get("thought", "");
    result.dialogue = json.get("dialogue", "");

    // 3. Extract parameterized actions
    for (auto& action_json : json["actions"]) {
        std::string name = action_json["action"];
        ActionID id = ActionRegistry::instance().lookup_name(name);
        if (id == ActionID::None) continue;  // Unknown action, skip

        ActionParams params;
        params.target_type = action_json.get("target", "");
        params.target_name = action_json.get("target", "");
        params.message = action_json.get("message", "");
        params.resource = action_json.get("resource", "");
        params.recipe = action_json.get("recipe", "");
        params.structure = action_json.get("structure", "");
        params.direction = action_json.get("direction", "");
        // Parse trade offer if present
        if (action_json.contains("offer")) {
            parse_trade_offer(action_json["offer"], params.trade_offer);
        }

        result.action_queue.push_back({id, params});
    }

    // 4. Validate: if no valid actions, default to Wander
    if (result.action_queue.empty()) {
        result.action_queue.push_back({ActionID::Wander, {}});
    }

    return result;
}
```

---

## 12. Action Skeleton Reference

### 12.1 Complete Action Table

| ActionID | Name | Description | Parameters | Prerequisites | Interruptible | Status |
|----------|------|-------------|------------|---------------|---------------|--------|
| Wander | Wander | Walk to random nearby location | `direction?` | None | Yes | **Existing** |
| Eat | Eat | Consume food to reduce hunger | `food?` | Nearby food source | No | **Existing** |
| Harvest | Harvest | Gather from plants/trees | `target?` | Nearby plant (soft) | No | **Existing** |
| Mine | Mine | Extract stone/ore from deposits | `target?` | Nearby deposit (soft) | No | Skeleton |
| Hunt | Hunt | Chase and kill animals | `target?` | Nearby prey (soft) | No | Skeleton |
| Build | Build | Construct a structure | `structure` | Accessible site | No | Skeleton |
| Sleep | Sleep | Rest to restore energy | `duration?` | None | Partially | Skeleton |
| Trade | Trade | Exchange items with entity | `target_name`, `trade_offer` | Nearby entity (soft) | No | Skeleton |
| Talk | Talk | Speak with nearby entity | `target_name`, `message` | Nearby entity (soft) | No | Skeleton |
| Craft | Craft | Create items from materials | `recipe` | Accessible material sources | No | Skeleton |
| Fish | Fish | Catch fish from water | — | Nearby water (soft) | No | Skeleton |
| Explore | Explore | Scout unknown territory | `direction?` | None | Yes | Skeleton |
| Flee | Flee | Run from danger | `direction?` | None | No | Skeleton |

**Soft prerequisite**: Checked internally by the action. If not met, fails immediately.
**Hard prerequisite**: Declared in `get_prerequisites()`, auto-resolved by `PrerequisiteResolver`.

### 12.2 Prerequisite Dependency Graph

**Note**: With infinite inventory, prerequisites focus on **availability and accessibility**
rather than "do I have enough?". The entity can always use its infinite resources; the
question is whether a suitable source is nearby.

```
Eat ----------> Nearby food source? --NO--> Harvest (berries/plants)
                                            OR Hunt (meat)

Craft ---------> Nearby material sources? --NO--> Harvest (wood, fiber)
                                                  OR Mine (stone, ore)
                                                  OR Trade (negotiate with entity)

Build ---------> Accessible building site? --YES--> Craft (if materials needed)
                                                    OR use infinite resources directly

Trade ---------> Nearby willing entity? (soft prereq, no auto-resolve)
Talk ----------> Nearby entity? (soft prereq)
Hunt ----------> Nearby prey? (soft prereq)
Mine ----------> Nearby deposit? (soft prereq)
Fish ----------> Nearby water? (soft prereq)

Wander --------> (no prerequisites)
Explore -------> (no prerequisites)
Flee ----------> (no prerequisites)
Sleep ---------> (no prerequisites)
```

### 12.3 Action Phase Outlines

**MineAction** `{target?: "stone_deposit"}`
- Phase 0: Find nearest mineable deposit (or the one specified in params). Pathfind to it.
- Phase 1: Wait for arrival.
- Phase 2: Emit `Mine` component. Duration-based extraction.
- Phase 3: Record mined resource in AIMemory (infinite inventory concept). Return `nextAction()`.

**HuntAction** `{target?: "bunny"}`
- Phase 0: Find nearest prey (or specified type). Pathfind toward it.
- Phase 1: Track prey movement. Re-pathfind if needed.
- Phase 2: When adjacent, emit `Attack` component.
- Phase 3: If kill successful, record in AIMemory (meat obtained). Return `nextAction()`.
- Failure: Prey escapes -> fail -> new LLM decision.

**BuildAction** `{structure: "house"}`
- Phase 0: Select build location near entity.
- Phase 1: Pathfind to build location.
- Phase 2: Emit `Build` component with structure type. Duration-based.
- Phase 3: Structure entity created in world. Return `nextAction()`.

**SleepAction** `{duration?: 300}`
- Phase 0: Find safe location (or sleep in place).
- Phase 1: Emit `Sleeping` component. Non-interruptible.
- Phase 2: Wait for energy to restore (duration-based).
- Phase 3: Remove `Sleeping`. Return `nextAction()`.

**TradeAction** `{target_name: "Grog", trade_offer: {give: "berry x5", want: "wood x2"}}`
- Phase 0: Find target entity by name. Pathfind to them.
- Phase 1: Arrive. Create Conversation with trade offer.
- Phase 2: Add offer message to conversation. Add event to target's AIMemory.
- Phase 3: Action completes. Trade resolution happens when target responds (see Section 8).

**TalkAction** `{target_name: "Grog", message: "Hello there!"}`
- Phase 0: Find target entity by name. Pathfind to them.
- Phase 1: Arrive. Create or continue Conversation.
- Phase 2: Add message to conversation. Set dialogue display. Add event to target's AIMemory.
- Phase 3: Action completes. Response happens asynchronously (see Section 8).

**CraftAction** `{recipe: "wooden_sword"}`
- Phase 0: Check if required materials are accessible (nearby sources or in infinite inventory).
- Phase 1: Emit `Crafting` component. Duration-based.
- Phase 2: Record crafted item in AIMemory. Return `nextAction()`.

**FishAction** `{}`
- Phase 0: Find nearest water tile. Pathfind to adjacent land.
- Phase 1: Arrive. Emit `Fishing` component.
- Phase 2: Wait duration with random success chance.
- Phase 3: If successful, record fish catch in AIMemory (infinite inventory concept). Return `nextAction()`.

**ExploreAction** `{direction?: "north"}`
- Phase 0: Pick direction (from params or random toward unexplored area).
- Phase 1: Create long-distance Path. Walk. Interruptible.
- Phase 2: When arrived, record explored area in AIMemory. Return `nextAction()`.

**FleeAction** `{direction?: "south"}`
- Phase 0: Identify threat or use specified direction.
- Phase 1: Pathfind away from threat, maximum speed.
- Phase 2: Run until safe distance. Non-interruptible.
- Phase 3: Return `nextAction()`.

---

## 13. Speech Bubbles & Action Display

### 13.1 Design

Each entity can display three types of text above them:
1. **Dialogue** (yellow): What the entity says aloud — `"Hey, got any berries?"`
2. **Thought** (gray, parenthesized): Internal LLM reasoning — `(hungry, should find food)`
3. **Current action** (blue, bracketed): What the entity is doing — `[Harvesting oak tree]`

These appear as floating ImGui windows positioned above the entity in screen space.

### 13.2 AIMemory Component

```cpp
struct AIEvent {
    ActionID action;
    bool succeeded;
    double timestamp;       // Game time
    std::string description;
};

struct ConversationEntry {
    entt::entity speaker;
    std::string text;
    double timestamp;
    bool read;              // Has this entity's LLM seen this message?
};

struct AIMemory {
    // Event history (for LLM context)
    std::vector<AIEvent> events;  // capped at 10

    // Conversation log
    std::vector<ConversationEntry> conversations;  // capped at 20

    // Active conversations
    std::vector<Conversation> active_conversations;

    // Display state (for speech bubbles)
    std::string current_thought;
    std::string current_dialogue;
    double dialogue_expires;      // game time when dialogue fades

    // Helper: get unread messages for prompt inclusion
    std::vector<const ConversationEntry*> get_unread() const;
    void mark_all_read();
};
```

### 13.3 SpeechBubbleSys

Registered in `game.cpp` as a pre-tick system (after AISys, before rendering). Follows
the exact pattern of the existing `ActionBarSys` which already renders ImGui overlays
at entity positions using `Camera::fromWorldSpace()`.

**Algorithm per frame:**
1. Get Camera and Input from registry context.
2. Iterate all entities with `AIMemory` + `Position`.
3. Convert entity world position to screen coordinates via `Camera::fromWorldSpace()`.
4. Frustum-cull: skip entities off-screen (with margin).
5. Render ImGui window at `(screen_x - offset, screen_y - height_offset)`:
   - Entity name (from Personality) as title
   - Dialogue text (if present and not expired) in yellow
   - Thought text (if present) in dim gray
   - Current action description (from `AIC::action->describe()`) in blue
6. Window flags: `NoDecoration | NoInputs | AlwaysAutoResize`.

### 13.4 Dialogue Expiration

Dialogue has TTL (e.g. 5 game-seconds). Thoughts persist until the next LLM decision.

---

## 14. LLM Configuration UI

### 14.1 ImGui Panel

```
+-- LLM Configuration ----------------------------------+
|                                                       |
|  Provider: [Ollama (localhost:11434)      v]         |
|  Model: [llama3.2                        v]          |
|  [+ Add Custom Provider]                              |
|                                                       |
|  -- Status --                                         |
|  Connection: * Connected                              |
|  Pending requests: 3                                  |
|  Avg latency: 342ms                                   |
|  Cache hit rate: 67% (124/185)                        |
|                                                       |
|  -- Settings --                                       |
|  Max RPS: [====5=====] 1-20                          |
|  Max concurrent: [==2===] 1-8                        |
|  Temperature: [===0.7===] 0.0-1.0                    |
|  [x] Humans use LLM  [ ] Animals use LLM            |
|  [ ] Enable request batching  Batch size: [4]        |
|                                                       |
|  [Clear Cache] [Test Connection]                      |
+-------------------------------------------------------+
```

### 14.2 Provider Management (ai-sdk-cpp)

All providers are managed through ai-sdk-cpp's unified interface. Providers are configured by URL + API key. Adding a new provider (e.g. OpenRouter, Together.ai) requires only updating settings. No code changes.

```cpp
struct LLMSettings {
    struct Provider {
        std::string name;           // Display name
        std::string base_url;       // API endpoint
        std::string api_key;        // Authentication key
        std::string model;          // Model identifier
        // ai-sdk-cpp handles routing transparently based on URL
    };

    std::vector<Provider> providers = {
        {"Ollama", "http://localhost:11434/v1", "", "llama3.2"},
        {"LM Studio", "http://localhost:1234/v1", "", "default"},
        {"Claude", "https://api.anthropic.com/v1", "<api-key>", "claude-3-5-sonnet-20241022"},
        {"OpenAI", "https://api.openai.com/v1", "<api-key>", "gpt-4o-mini"},
    };
    int active_provider = 0;  // Active provider index

    // Rate limiting & concurrency
    int rate_limit_rps = 5;
    int max_concurrent = 2;
    float temperature = 0.7f;

    // Feature flags
    bool humans_use_llm = true;
    bool animals_use_llm = false;

    // Optional batching (flag-controlled)
    bool batching_enabled = false;
    int batch_size = 4;
    int batch_window_ms = 50;

    template<class Archive>
    void serialize(Archive& ar) { /* cereal fields */ }
};
```

**Key point**: ai-sdk-cpp automatically detects whether a provider is OpenAI-compatible, Anthropic, or other format based on the API structure. The `LLMClient` wrapper doesn't need special handling per provider.

---

## 15. Migration Path

### 15.1 Preserving Existing Behavior

The existing score-based system is **not removed**. It becomes the fallback:

| Component | Before | After |
|-----------|--------|-------|
| `AIC` | `action`, `score` | + `action_queue`, `waiting_for_llm`, `use_llm` |
| `AISys::tick()` | Always calls `decide()` | Calls `decide()` only when LLM unavailable |
| `AISys::decide()` | Unchanged | Unchanged |
| `WanderBehavior` | Unchanged | Unchanged (fallback) |
| `EatBehavior` | Unchanged | Unchanged (fallback) |

### 15.2 Migrating Existing Actions

**WanderAction**:
- Inherit `ActionBase<WanderAction, ActionID::Wander>` instead of `Action`
- Add static metadata methods and `describe()`
- Constructor accepts `ActionParams` (direction param optional)
- `act()` logic unchanged

**EatAction**:
- Inherit `ActionBase<EatAction, ActionID::Eat>`
- Constructor accepts `ActionParams` (food type param)
- Add prerequisites: checks inventory for food, resolves to Harvest
- **Remove `deploy()`** — chaining handled by `PrerequisiteResolver`
- `act()` logic mostly unchanged

**HarvestAction**:
- Inherit `ActionBase<HarvestAction, ActionID::Harvest>`
- Constructor accepts `ActionParams` (target plant type)
- Uses `params.target_type` to find specific plant if specified
- `act()` logic unchanged, but target selection enhanced by params

---

## 16. File Structure

```
src/ai/
|-- aic.h                              # MODIFIED: add action_queue, waiting_for_llm, use_llm
|-- ai.h / ai.cpp                      # MODIFIED: dual-mode tick(), conversation handling
|
|-- llm/
|   |-- llm_client.h                   # Thin wrapper around ai-sdk-cpp (sole HTTP client)
|   |-- llm_client.cpp                 # Uses ai_sdk_cpp::Client under the hood
|   |-- llm_manager.h                  # Central orchestrator: queues, workers, rate limit, batching
|   |-- llm_manager.cpp
|   |-- prompt_builder.h               # Prompt construction with personality + conversations
|   |-- prompt_builder.cpp
|   |-- response_parser.h              # Parse LLM JSON -> parameterized actions
|   |-- response_parser.cpp
|   +-- decision_cache.h               # Response caching with discretized keys
|
|-- actions/
|   |-- action.h                       # MODIFIED: add ActionID, describe(), ActionParams
|   |-- action_params.h                # ActionParams, TradeOffer structs
|   |-- action_registry.h / .cpp       # Singleton action type registry
|   |-- action_factory.h               # CRTP ActionBase template
|   |-- prerequisite_resolver.h / .cpp # Recursive prerequisite chaining
|   |
|   |-- wander_action.h / .cpp         # MODIFIED: use ActionBase
|   |-- eat_action.h / .cpp            # MODIFIED: use ActionBase, remove deploy()
|   |-- harvest_action.h / .cpp        # MODIFIED: use ActionBase
|   |
|   |-- mine_action.h / .cpp           # NEW skeleton
|   |-- hunt_action.h / .cpp
|   |-- build_action.h / .cpp
|   |-- sleep_action.h / .cpp
|   |-- trade_action.h / .cpp          # Conversation-creating action
|   |-- talk_action.h / .cpp           # Conversation-creating action
|   |-- craft_action.h / .cpp
|   |-- fish_action.h / .cpp
|   |-- explore_action.h / .cpp
|   +-- flee_action.h / .cpp
|
|-- behaviors/                          # UNCHANGED -- fallback system
|   |-- behavior.h
|   |-- wander_behavior.h / .cpp
|   +-- eat_behavior.h / .cpp
|
|-- conversation/
|   |-- conversation.h                 # Conversation, ConversationMessage structs
|   +-- conversation_manager.h / .cpp  # Track active conversations, timeout handling
|
|-- personality/
|   |-- personality.h                  # Personality component
|   +-- personality_generator.h / .cpp # Random + AI-assisted generation
|
+-- speech/
    |-- speech_bubble_sys.h / .cpp     # ImGui overlay system
    +-- ai_memory.h                    # AIMemory component (events + conversations)

lib/
+-- ai-sdk-cpp/                        # NEW git submodule

Build files:
|-- vcpkg.json                         # No changes needed (ai-sdk-cpp is self-contained)
+-- CMakeLists.txt                     # ADD: ai-sdk-cpp subdirectory, target_link_libraries
```

---

## 17. Performance

### 17.1 The Core Problem

With 50 entities using LLM and 200ms average response time:
- Naive: 50 entities x 1 request each = 50 requests per decision cycle
- At 5 RPS: 10 seconds to serve all entities

### 17.2 Mitigation Strategies

#### Strategy 1: Action Queue (Most Impactful)

The LLM returns 3-5 actions per request. If an entity works through 4 actions averaging
8 seconds each, it needs a new decision every 32 seconds.

- 50 entities / 32 seconds = 1.56 RPS average
- Well within 5 RPS limit

#### Strategy 2: Decision Cache

Discretize entity state into buckets:
- Hunger: low (0-3), medium (4-6), high (7-10)
- Has food: yes/no
- Nearby entity types: bitfield
- Personality archetype: hash of discretized traits

Expected cache hit rate: 40-70%. **Note**: Cache is less effective when personalities
are highly varied, since personality is part of the cache key.

#### Strategy 3: Optional Batching

When enabled, accumulate 2-4 entities per prompt. Reduces HTTP overhead.
Off by default (individual calls produce better per-personality results).

#### Strategy 4: Rate Limiting

Token bucket algorithm. Configurable RPS via UI. Prevents overwhelming the LLM server.

#### Strategy 5: Fallback Actions

Entities always have a valid action. While waiting for LLM: continue current task or
wander. No visible "thinking freeze."

#### Strategy 6: Priority Queue

Urgent needs (critical hunger, incoming trade offer) get higher priority.
Entities with 3+ actions still queued get lower priority.

#### Strategy 7: Per-Entity Toggle

Only Humans use LLM; animals use score-based fallback. Configurable per entity type.

### 17.3 Steady-State Analysis

**Conservative scenario** (50 human entities, 200ms latency, 2 workers):

| Parameter | Value |
|-----------|-------|
| Entities | 50 |
| Actions per LLM call | 4 average |
| Action duration | 8s average |
| Decision interval | 32s per entity |
| Request rate | 1.56 RPS |
| Cache hit rate | 50% |
| Actual request rate | 0.78 RPS |
| Throughput (2 workers) | 10 RPS |
| **Queue depth** | **< 1 (empty)** |

**Worst case** (200 entities, 1000ms latency, 1 worker, no cache):

| Parameter | Value |
|-----------|-------|
| Request rate | 6.25 RPS |
| Throughput | 1 RPS |
| Queue growth | 5.25/sec |

Mitigations: enable caching, enable batching, use faster model, increase workers,
reduce LLM entity count.

### 17.4 Conversation Performance Impact

Conversations generate additional LLM requests (one per message sent). However:
- Conversations are naturally rate-limited by entity behavior (entities do other actions between messages)
- Most conversations are 2-4 messages total
- Only active conversations generate requests
- Expected overhead: +10-20% on top of base decision requests

---

## 18. Problems & Solutions

### Problem 1: LLM Response Latency

**Issue**: 200ms-2000ms per response. Game runs at 16ms/frame.

**Solution**: Fully async. Worker threads handle HTTP. Entities continue tasks while
waiting. Results polled per-frame.

### Problem 2: LLM Hallucination

**Issue**: LLM returns invalid action names or malformed JSON.

**Solution**: Strict validation. Only recognized `ActionID` names pass through
`ActionRegistry`. Unknown names silently dropped. Malformed JSON defaults to `[Wander]`.
Invalid parameters cause action failure at Phase 0, recorded in AIMemory for context.

### Problem 3: Entity Destroyed During Async Wait

**Issue**: Entity dies between submission and result delivery.

**Solution**: `reg.valid(result.entity)` check. Invalid results discarded silently.

### Problem 4: Conversations Between Destroyed Entities

**Issue**: Entity A sends message, Entity B dies before seeing it.

**Solution**: `ConversationManager` checks entity validity each tick. Conversations with
invalid entities are marked Concluded. Surviving entity gets event: "Entity X is gone."

### Problem 5: Cost Management (Cloud APIs)

**Issue**: Claude API charges per token.

**Solution**: Default to local models (free). Cloud API opt-in with cost awareness in UI.
Cache, action queues, and per-entity toggle reduce volume 4-10x.

### Problem 6: LLM Server Unavailable

**Issue**: Ollama not running, API key invalid.

**Solution**: Health check via `is_available()`. Automatic fallback to score-based
`decide()`. UI shows connection status. No crash, no error spam.

### Problem 7: Thread Safety

**Issue**: Worker threads and game thread share queues.

**Solution**: `ThreadSafe<T>` wrapper from `src/util/util.h`. Workers NEVER access
`entt::registry`. All world state serialized to strings on the game thread before submission.

### Problem 8: Prerequisite Cycles

**Issue**: Trade->needs gold->Mine->needs pickaxe->Craft->needs iron->Trade...

**Solution**: `visited` set + `max_depth` limit (5) in recursive resolver. Design
prerequisites to avoid cycles (basic tools craftable from basic materials).

### Problem 9: Personality Consistency

**Issue**: LLM might not consistently follow personality across decisions.

**Solution**: Personality is in the system prompt of every request. Reinforced by
speech style instruction. For strong consistency, include 1-2 example decisions in the
personality prompt.

### Problem 10: Trade Fairness / Exploitation

**Issue**: LLM might accept terrible trade deals or always reject.

**Solution**: Include inventory value hints in the prompt: "Your berries are worth ~1
each, wood is worth ~3 each." The personality (generosity trait) also influences this.

### Problem 11: Conversation Spam

**Issue**: Two talkative entities could endlessly ping-pong Talk actions.

**Solution**: Conversation cooldown: same two entities can't start a new conversation
within 60 game-seconds of the last one. Conversation max length: 6 messages, then
auto-conclude.

### Problem 12: Cold Start

**Issue**: All entities need decisions simultaneously at game start.

**Solution**: Stagger initial decisions with random 0-10s delays. Entities wander during
this period.

### Problem 13: Model Quality Variance

**Issue**: Small local models produce lower-quality decisions than Claude.

**Solution**: Carefully crafted prompt with explicit JSON format, valid action list with
examples, and simple output structure. For very small models, simplify prompt (remove
thought/dialogue, just request action list). Configurable per-provider.

---

## 19. Implementation Phases

### Phase 1: Action System Foundation (No LLM)

- Add `ActionID` enum, `ActionParams`, `TradeOffer`
- Create `ActionDescriptor`, `ActionPrerequisite`, `ActionRegistry`
- Create `ActionBase<>` CRTP template
- Migrate `WanderAction`, `EatAction`, `HarvestAction` to `ActionBase`
- Create `PrerequisiteResolver`
- Create skeleton action classes for all 10 new actions
- Verify game still works identically

### Phase 2: Personality & Memory

- Create `Personality` component and random generator
- Create `AIMemory` component
- Create `Conversation` structs and `ConversationManager`
- Assign personalities to existing human entities on spawn
- Test serialization via cereal

### Phase 3: LLM Infrastructure (ai-sdk-cpp)

- **Add ai-sdk-cpp as git submodule** under `lib/ai-sdk-cpp` (sole HTTP client library)
- Update CMakeLists.txt to build ai-sdk-cpp and link target
- Create `LLMClient` wrapper (abstracts ai-sdk-cpp, handles provider config, request batching flag)
- Create `LLMManager` with worker threads, queues, rate limiting, optional batching
- Create `PromptBuilder` (with personality and conversation support)
- Create `ResponseParser` (parameterized action extraction from LLM JSON)
- Create `DecisionCache` for decision reuse
- Test standalone: ai-sdk-cpp client -> prompt -> LLM -> parsed actions (all providers)

### Phase 4: Game Integration

- Expand `AIC` component with `action_queue`, `waiting_for_llm`, `use_llm`
- Modify `AISys::tick()` with dual-mode logic
- Add `LLMManager` to registry context in `game.cpp`
- Add LLM settings to `Settings` class
- Implement conversation handling in AISys (incoming messages, Trade resolution)
- Test: entities make LLM decisions, execute parameterized actions, converse
- Test: fallback when LLM is off

### Phase 5: UI

- Create `SpeechBubbleSys` (ImGui overlays with name, dialogue, thought, action)
- Add LLM configuration panel (provider switching, stats, batching toggle)
- Add Personality editor panel
- Register systems in game.cpp

### Phase 6: Action Implementation

- Implement each skeleton action one at a time
- Add corresponding ECS systems (MineSys, BuildSys, CraftSys, etc.)
- Add new components as needed
- Expand Item enum and Object types
- Test each action individually and in combination with LLM decisions

---

## 20. Architectural Highlights & Key Changes

### ai-sdk-cpp as Sole HTTP Client

- **No manual HTTP boilerplate**: All LLM communication flows through ai-sdk-cpp
- **Provider transparency**: Ollama, LM Studio, Claude, OpenAI, and OpenAI-compatible endpoints all use the same client interface
- **Configuration-driven**: Switching providers requires only editing settings (base_url, api_key, model), not code changes
- **Single integration point**: `LLMClient` wrapper abstracts ai-sdk-cpp; all LLM calls go through it

### Infinite Inventory

- **No Storage component**: Entities don't have physical item containers
- **Narrative tracking**: Harvested/mined/hunted resources are recorded in AIMemory as events
- **Prerequisites focus on accessibility**: "Is there a tree nearby?" instead of "Do I have enough wood?"
- **Trade is conceptual**: Entities exchange resources via dialogue; no physical transfer, only dialogue recorded

### Action Parameters & Trade/Speech

- **Precise action specifications**: Actions carry parameters (target, offer, message) from LLM
- **Trade/Speech as conversations**: Two-entity interactions happen asynchronously via LLM responses to incoming messages
- **Conversation state machine**: Messages trigger new LLM requests when entities see them
- **AIMemory tracks dialogue**: Past conversations are included in future LLM prompts

### Personality & Uniqueness

- **Randomly generated at spawn**: Each entity gets unique traits (friendliness, industriousness, curiosity, etc.)
- **Shapes LLM decisions**: Same state → different behaviors for different personalities
- **Hand-editable or AI-generated**: Personalities can be customized or regenerated
- **Persistent across decisions**: Personality is in every LLM prompt

### Optional Batching (Flag-Controlled)

- **Default: disabled** — Individual per-entity prompts produce better personality-specific results
- **When enabled**: Accumulates 2-4 entities per prompt window, reducing HTTP overhead
- **Configurable at runtime**: Toggle via UI without restarting
- **Cache-aware**: Batching respects decision cache hits; only new decisions are batched

---

*Document version: 2.2 — March 2026*
*For the Dynamical simulation engine*
*LLM API: ai-sdk-cpp (ClickHouse) — sole HTTP client library*

**v2.2 Changes**: Simplified ai-sdk-cpp integration requirements. Clarified that:
- ai-sdk-cpp natively supports Anthropic Claude (no special endpoint handling needed)
- Bundled nlohmann_json removes vcpkg dependency burden
- LLMClient wrapper is for game-specific logic (personality injection, action parsing, caching), not SDK gaps
- No additional external dependencies beyond C++20 compiler and CMake 3.16+
