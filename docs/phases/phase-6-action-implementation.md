# Phase 6: Action Implementation

**Goal**: Implement all 10+ action skeletons with full game logic, prerequisites, and state machines. Create corresponding ECS systems.

**Scope**: The longest phase — core gameplay functionality.

**Estimated Scope**: ~3000 lines of new code

**Completion Criteria**: All actions are functional, have realistic prerequisites, execute in game, and integrate with Personality & Memory systems.

---

## Context: Why This Phase Matters

Phases 1-5 built the infrastructure. Phase 6 fills it with content.

The LLM can request any action with parameters, but without implementations, the game is empty. This phase implements each action as a state machine with:
- Phase 0: Initialization (resolve parameters, find targets)
- Phase 1+: Execution (move, collect, interact)
- Completion: Record in AIMemory, emit event

---

## 1. Action Implementation Pattern

Every action follows this template:

### 1.1 Header Template

```cpp
// src/ai/actions/my_action.h
#pragma once
#include "action_factory.h"

class MyAction : public ActionBase<MyAction, ActionID::MyAction> {
public:
    MyAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});

    static const char* action_name() { return "MyAction"; }
    static const char* action_description() { return "Does something..."; }
    static std::vector<ActionPrerequisite> get_prerequisites();

    std::string describe() const override;
    std::unique_ptr<Action> act(std::unique_ptr<Action> self) override;

private:
    enum class Phase {
        Initialize,
        InProgress,
        Complete
    } phase = Phase::Initialize;

    // State tracking
    entt::entity target;
    float progress = 0.0f;
};
```

### 1.2 Implementation Template

```cpp
// src/ai/actions/my_action.cpp
#include "my_action.h"
#include "../aic.h"
#include "../../logic/components/*.h"

MyAction::MyAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params) {
    // Initialize based on params
}

std::vector<ActionPrerequisite> MyAction::get_prerequisites() {
    return {
        // List prerequisite checks and resolvers
    };
}

std::string MyAction::describe() const {
    switch (phase) {
        case Phase::Initialize: return "Setting up...";
        case Phase::InProgress: return "Working...";
        case Phase::Complete: return "Done!";
    }
    return "Unknown";
}

std::unique_ptr<Action> MyAction::act(std::unique_ptr<Action> self) {
    switch (phase) {
        case Phase::Initialize:
            // Find target, set up state
            phase = Phase::InProgress;
            return self;

        case Phase::InProgress:
            // Do work, check completion
            progress += 0.5f;
            if (progress >= 1.0f) {
                phase = Phase::Complete;
            }
            return self;

        case Phase::Complete:
            // Record event, return next action
            if (auto* mem = reg.try_get<AIMemory>(entity)) {
                mem->add_event("action_completed", "Did something useful");
            }
            return nextAction();
    }
    return self;
}
```

---

## 2. Specific Action Implementations

### 2.1 Mine Action

**Goal**: Extract ore/stone from deposits.

**Parameters**: `target_type` (deposit), `resource` (ore/stone)

**Prerequisites**:
- Has a mine deposit nearby (optional: has pickaxe)

**State Machine**:
- Phase 0: Find deposit, navigate to it
- Phase 1: Extract resource (animation/progress)
- Phase 2: Record in AIMemory as event

```cpp
// src/ai/actions/mine_action.h
#pragma once
#include "action_factory.h"

class MineAction : public ActionBase<MineAction, ActionID::Mine> {
public:
    MineAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});

    static const char* action_name() { return "Mine"; }
    static const char* action_description() { return "Extract minerals from deposits"; }
    static std::vector<ActionPrerequisite> get_prerequisites();

    std::string describe() const override;
    std::unique_ptr<Action> act(std::unique_ptr<Action> self) override;

private:
    enum class Phase { Initialize, Pathfind, Mine, Complete } phase = Phase::Initialize;
    entt::entity target_deposit;
    float progress = 0.0f;
    int amount = 1;
};
```

### 2.2 Hunt Action

**Goal**: Track and hunt animals for meat.

**Parameters**: `target_type` (bunny/deer/etc)

**Prerequisites**:
- Animal exists and is huntable

**State Machine**:
- Phase 0: Locate animal
- Phase 1: Stalk/chase (use PathComponent to move toward target)
- Phase 2: Kill and collect meat
- Record in AIMemory

### 2.3 Build Action

**Goal**: Construct structures (house, wall, storage).

**Parameters**: `structure` (house/wall/storage), position implicit (near entity)

**Prerequisites**:
- Has resources for construction
- Valid build location

**State Machine**:
- Phase 0: Validate location, gather materials
- Phase 1: Build (progress over time)
- Phase 2: Complete, create structure in world

### 2.4 Sleep Action

**Goal**: Rest to restore energy.

**Parameters**: `duration` (seconds, default 30)

**Prerequisites**: None

**State Machine**:
- Phase 0: Find resting spot (or use current location)
- Phase 1: Sleep (increment energy component)
- Phase 2: Wake up when energy full or duration elapsed

### 2.5 Trade Action

**Goal**: Exchange resources with another entity.

**Parameters**: `target_name`, `trade_offer` (give/want)

**Prerequisites**:
- Target entity exists and is willing to trade

**State Machine**:
- Phase 0: Find target entity
- Phase 1: Create conversation, send trade offer
- Phase 2: Wait for response (async LLM call for target)
- Phase 3: Resolve trade (record in both entities' AIMemory)

```cpp
// Trade action is special: creates a Conversation
class TradeAction : public ActionBase<TradeAction, ActionID::Trade> {
public:
    TradeAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});

    static const char* action_name() { return "Trade"; }
    static const char* action_description() { return "Exchange resources with another entity"; }
    static std::vector<ActionPrerequisite> get_prerequisites();

    std::string describe() const override;
    std::unique_ptr<Action> act(std::unique_ptr<Action> self) override;

private:
    enum class Phase {
        Initialize,
        FindTarget,
        SendOffer,
        WaitingForResponse,
        Complete
    } phase = Phase::Initialize;

    entt::entity target_entity = entt::null;
    Conversation* active_conversation = nullptr;
    float timeout = 30.0f;
    float elapsed = 0.0f;
};
```

### 2.6 Talk Action

**Goal**: Converse with another entity.

**Parameters**: `target_name`, `message`

**Prerequisites**:
- Target entity exists

**State Machine**:
- Phase 0: Find target
- Phase 1: Send message via conversation
- Phase 2: Await response (async LLM on target)
- Phase 3: Record dialogue in AIMemory

### 2.7 Craft Action

**Goal**: Create items from resources.

**Parameters**: `recipe` (wooden_sword/stone_pickaxe/etc)

**Prerequisites**:
- Has required materials
- Knows recipe

**State Machine**:
- Phase 0: Validate recipe and materials
- Phase 1: Craft (progress over time)
- Phase 2: Complete, record in AIMemory

### 2.8 Fish Action

**Goal**: Catch fish from water.

**Parameters**: None (implicit: find nearest water)

**Prerequisites**:
- Water tile nearby

**State Machine**:
- Phase 0: Find water
- Phase 1: Cast line and wait (random catch timer)
- Phase 2: Reel in, collect fish

### 2.9 Explore Action

**Goal**: Discover new areas.

**Parameters**: `direction` (optional: north/south/east/west, else random)

**Prerequisites**: None

**State Machine**:
- Phase 0: Pick direction (use params or random)
- Phase 1: Move in direction (longer than normal wander)
- Phase 2: Record new areas in AIMemory

### 2.10 Flee Action

**Goal**: Run away from danger.

**Parameters**: `direction` (away from threat), optional speed modifier

**Prerequisites**:
- Perceived threat (entity with low trust, large health pool, etc.)

**State Machine**:
- Phase 0: Identify threat direction
- Phase 1: Move away at high speed
- Phase 2: Stop when threat is far or time elapsed

---

## 3. Prerequisite Examples

### 3.1 "Need Food" Prerequisites

```cpp
std::vector<ActionPrerequisite> EatAction::get_prerequisites() {
    return {
        ActionPrerequisite{
            .is_satisfied = [](entt::registry& reg, entt::entity entity) {
                // Check if entity has food in AIMemory or nearby plants
                auto* mem = reg.try_get<AIMemory>(entity);
                if (!mem) return false;

                // Did we recently harvest food?
                for (const auto& event : mem->events) {
                    if (event.event_type == "harvested" && !event.seen_by_llm) {
                        return true;  // Have unseen harvested food
                    }
                }
                return false;
            },
            .resolve_action = [](entt::registry& reg, entt::entity entity) {
                // If no food, harvest it
                return ActionID::Harvest;
            },
            .description = "needs food"
        }
    };
}
```

### 3.2 "Need Pickaxe" Prerequisites

```cpp
ActionPrerequisite{
    .is_satisfied = [](entt::registry& reg, entt::entity entity) {
        // Check if entity has crafted a pickaxe recently
        auto* mem = reg.try_get<AIMemory>(entity);
        if (!mem) return false;

        for (const auto& event : mem->events) {
            if (event.event_type == "crafted" && event.description.find("pickaxe") != std::string::npos) {
                return true;
            }
        }
        return false;
    },
    .resolve_action = [](entt::registry& reg, entt::entity entity) {
        // Craft a pickaxe
        return ActionID::Craft;
    },
    .description = "needs pickaxe"
}
```

---

## 4. Action Parameters Mapping

| Action | Params | Example |
|--------|--------|---------|
| **Wander** | — | `{}` |
| **Eat** | food (optional) | `{"food": "berry"}` |
| **Harvest** | target_type (optional) | `{"target": "oak_tree"}` |
| **Mine** | target_type, resource | `{"target": "stone_deposit", "resource": "ore"}` |
| **Hunt** | target_type | `{"target": "bunny"}` |
| **Build** | structure, (position) | `{"structure": "house"}` |
| **Sleep** | duration (optional) | `{"duration": 30}` |
| **Trade** | target_name, trade_offer | `{"target": "Entity#42", "offer": {...}}` |
| **Talk** | target_name, message | `{"target": "Entity#42", "message": "Hello!"}` |
| **Craft** | recipe | `{"recipe": "wooden_sword"}` |
| **Fish** | — | `{}` |
| **Explore** | direction (optional) | `{"direction": "north"}` |
| **Flee** | direction, threat | `{"direction": "south"}` |

---

## 5. ECS Systems for Actions

For each action type that requires ongoing logic, create a corresponding system:

### 5.1 MineSys

```cpp
// src/logic/systems/mine_sys.h
#pragma once
#include "system.h"

class MineSys : public System {
public:
    void pre_tick(float dt) override;
    void post_tick(float dt) override;

private:
    // Handle mining progress, resource collection, etc.
};
```

Register in game loop:

```cpp
systems->register_system<MineSys>();
```

### 5.2 Similar Systems for Other Actions

- HuntSys
- BuildSys (for structure placement and physics)
- CraftSys (for crafting queues)
- FishSys (for catch timers)
- ConversationSys (for Trade/Talk async handling)

---

## 6. Integration with AIMemory

When action completes, record in AIMemory:

```cpp
if (phase == Phase::Complete) {
    if (auto* mem = reg.try_get<AIMemory>(entity)) {
        mem->add_event("harvested", "Collected 5 berries from bush");
    }
    return nextAction();
}
```

These events are included in LLM prompts for context.

---

## 7. Conversation-Based Actions

Trade and Talk actions are special — they create async conversations:

### 7.1 Trade Action Flow

```
1. Entity A executes Trade action with target B, offer {give: 5 berries, want: 2 wood}
2. Trade action creates Conversation(A, B) and sends first message
3. B's AIMemory receives message as unread_message
4. B's next LLM decision includes unread messages
5. B decides to accept/reject and responds
6. A receives response, Trade action completes or continues
7. Both AIMemories record trade event
```

### 7.2 Talk Action Flow

```
1. Entity A executes Talk with message "Hello, how are you?"
2. Creates Conversation(A, B), sends message
3. B receives unread message
4. B's LLM generates response
5. B sends response, A receives it
6. Conversation concludes (both record in AIMemory)
```

---

## 8. Implementation Checklist

### Phase 0: Skeleton All Actions
- [ ] All 13 action headers created with ActionBase template
- [ ] All action names and descriptions defined
- [ ] Prerequisite stubs (empty vectors for now)
- [ ] Describe() methods return placeholder text
- [ ] Act() methods have phase enum with Initialize/InProgress/Complete

### Phase 1: Implement Core Actions (in order)
- [ ] Wander (modify existing)
- [ ] Eat (modify existing)
- [ ] Harvest (modify existing)
- [ ] Sleep (simplest new action)
- [ ] Explore (variant of Wander)

### Phase 2: Implement Gathering Actions
- [ ] Mine (with resource extraction)
- [ ] Hunt (with target tracking)
- [ ] Fish (with random catch timer)
- [ ] Craft (with recipe lookup and resource consumption)

### Phase 3: Implement Interaction Actions
- [ ] Build (structure placement)
- [ ] Trade (conversation-based, async)
- [ ] Talk (conversation-based, async)
- [ ] Flee (directional movement)

### Phase 4: Create Corresponding Systems
- [ ] MineSys (update progress, emit resources)
- [ ] HuntSys (target tracking, damage calculation)
- [ ] BuildSys (structure placement, collision)
- [ ] CraftSys (recipe validation, resource consumption)
- [ ] ConversationSys (async response handling)

### Phase 5: Test & Polish
- [ ] Each action reaches completion state
- [ ] Prerequisite chaining works end-to-end
- [ ] Actions record in AIMemory correctly
- [ ] LLM decisions trigger correct action sequences
- [ ] No game crashes or physics glitches

---

## 9. Testing Checklist

### Unit Tests (Per Action)
- [ ] Action initializes with correct params
- [ ] Prerequisite checks work (satisfied + resolve)
- [ ] Act() transitions through all phases
- [ ] Describe() returns valid text for each phase
- [ ] AIMemory events recorded on completion

### Integration Tests
- [ ] LLM requests Mine → MineAction created → MineSys processes → event recorded
- [ ] Hungry entity → LLM requests Eat → resolves Harvest prerequisite → harvests → eats
- [ ] Entity decides to Trade → Trade action → conversation created → target receives message
- [ ] Build action → BuildSys places structure → visible in world

### Play-Test
- [ ] Manual LLM prompts for each action type
- [ ] Personality shapes action execution (generous entity trades fairly, curious explores more)
- [ ] Action chains work (Harvest → Eat, Craft pickaxe → Mine)
- [ ] No infinite loops or softlocks

---

## 10. Performance Considerations

- **Action updates per frame**: ~100 entities × multiple actions = moderate CPU cost
- **Spatial queries**: Mining/hunting require nearby entity detection → use spatial hash
- **Conversation async**: Trade/Talk spawn new LLM requests → rate limiting applies
- **AIMemory growth**: Events accumulate → periodically prune old events (beyond 100)

---

## 11. Extensibility

After Phase 6, adding new actions is trivial:

1. Add to `ActionID` enum
2. Create `NewAction` class inheriting `ActionBase`
3. Implement `act()` state machine
4. (Optional) Create `NewSys` ECS system if complex state needed
5. Done — LLM can request it immediately

---

## 12. Dependencies

- **Depends on**: Phases 1-5 (all infrastructure complete)
- **Blocks**: Nothing (end of implementation)
- **Final phase**: Game is fully playable after this phase

---

## 13. Timeline

Expected implementation order:
1. **Week 1**: Skeleton all actions, implement Wander/Eat/Harvest
2. **Week 2**: Implement Sleep, Explore, Wander variants
3. **Week 3**: Mine, Hunt, Fish with resource systems
4. **Week 4**: Build, Craft with structure/recipe systems
5. **Week 5**: Trade, Talk with conversation integration
6. **Week 6**: Testing, polish, bug fixes

---

## 14. Debugging Tips

- **Use SpeechBubbleSys** to visualize action descriptions
- **Check AIMemory** for event sequence (what did the action record?)
- **Inspect LLM prompt** to see what state the LLM saw
- **Enable action logging**: Print each phase transition
- **Stub conversation responses**: For testing Trade/Talk without full LLM

---

**Reference Sections from Global Doc:**
- Section 5.4: Action Skeleton Reference
- Section 6.3: Parameters Per Action
- Section 8: Conversation System
- Problem/Solution sections: #8 (cycles), #10 (trade fairness), #11 (spam)
