# Phase 6: Action Implementations

Comprehensive implementation guide for all 10 gameplay actions. Each action has its own doc file with full infrastructure requirements, code sketches, and integration details.

## Implementation Order

Actions are ordered by complexity. Each tier builds on the infrastructure introduced by the previous tier.

| Tier | Action | Doc | New Components | New Systems | New Items | Complexity |
|------|--------|-----|----------------|-------------|-----------|------------|
| 1 | **Sleep** | [sleep.md](sleep.md) | `Sleeping` | `SleepSys` | — | Low |
| 1 | **Explore** | [explore.md](explore.md) | — | — | — | Low |
| 1 | **Flee** | [flee.md](flee.md) | — | — | — | Low |
| 2 | **Mine** | [mine.md](mine.md) | `Mine`, `Mined` | `MineSys` | `stone`, `ore` | Medium |
| 2 | **Fish** | [fish.md](fish.md) | `Fishing` | `FishSys` | `fish` | Medium |
| 2 | **Hunt** | [hunt.md](hunt.md) | — | — | `meat` | Medium |
| 3 | **Craft** | [craft.md](craft.md) | `Crafting` | `CraftSys` | `wood`, `plank`, tools | High |
| 3 | **Talk** | [talk.md](talk.md) | — | — | — | High |
| 3 | **Trade** | [trade.md](trade.md) | — | — | — | High |
| 4 | **Build** | [build.md](build.md) | `Building`, `Structure` | `BuildSys` | — | High |

## Shared Infrastructure

These changes are needed across multiple actions. Apply them as each action introduces them.

### Item::ID Additions (`src/logic/components/item.h`) ✓ Done

All items below are defined. New items must be added to the `DY_ITEMS(X)` macro in `item.h` — the enum, `to_string`, and `from_string` derive from it automatically.

| Item | Introduced by | Used by |
|------|---------------|---------|
| `wood` | Craft | Craft, Build, Craft recipes |
| `stone` | Mine | Mine, Craft, Build |
| `ore` | Mine | Mine, Craft |
| `fish` | Fish | Fish, Craft (cooked_fish) |
| `meat` | Hunt | Hunt, Craft (cooked_meat) |
| `plank` | Craft | Craft, Build |
| `wooden_sword` | Craft | Craft |
| `stone_pickaxe` | Craft | Craft |
| `cooked_meat` | Craft | Craft, Eat |
| `cooked_fish` | Craft | Craft, Eat |

`Item::to_string(ID)` — O(1) array index. `Item::from_string(std::string_view)` — O(1) compile-time FNV-1a hash table (29-slot open-addressing, prime-sized). Both are `constexpr`.

### Object::Identifier Additions (`src/logic/components/object.h`)

| Identifier | Type | Introduced by |
|------------|------|---------------|
| `stone_deposit` | `resource` | Mine |
| `ore_deposit` | `resource` | Mine |
| `fish_spot` | `resource` | Fish |
| `structure` | `building` | Build |

Also add `Object::Type` values: `resource`, `building`.

### BasicNeeds Extension (`src/logic/components/basic_needs.h`)

Add `float energy = 10;` — drained over time by `BasicNeedsSys`, restored by Sleep. Update `BasicNeedsSys::tick()` to drain energy at the same rate as hunger (10 units per game day).

### ConversationManager Context (`src/logic/game.cpp`)

Talk and Trade actions need `ConversationManager` access from within actions. It's already registered as `reg.set<ConversationManager>(reg)`. Actions access it via `this->reg.ctx<ConversationManager>()`.

### System Registration Pattern

For each new system:
1. Add `DEFINE_SYSTEM(XxxSys)` to `src/logic/systems/system_list.h`
2. Add `set->pre_add<XxxSys>()` to `src/logic/game.cpp` (after `EatSys`, before `AISys`)
3. Create `src/logic/systems/xxx.cpp` implementing `preinit()`, `init()`, `tick()`, `finish()`

## Reference Patterns

### HarvestAction (4-stage reference)

The gold standard pattern for resource-gathering actions:

```
src/ai/actions/harvest_action.h   — header with ActionStageMachine
src/ai/actions/harvest_action.cpp — 4 stages: find → pathfind → emplace → wait
src/logic/components/harvest.h    — component: entity target + time_t start
src/logic/systems/harvest.cpp     — system: duration check, grant items, cooldown tag
```

Key patterns:
- `this->reg`, `this->entity` in lambdas (MSVC C3493)
- `entt::tag<"reserved"_hs>` to prevent multiple entities targeting same resource
- `stage_primitives::wait_until_removed<C>(reg, entity)` for system handoff
- `stage_primitives::wait_for_seconds(n)` for timed waits
- `ActionBar(start, start + duration)` for visual progress

### EatAction (2-stage reference)

Simpler pattern for inventory-based actions:

```
src/ai/actions/eat_action.h/.cpp  — 2 stages: find food → wait for Eat removal
src/logic/components/eat.h        — component: Item::ID food + time_t start
src/logic/systems/eat.cpp         — system: nutrition calc, Storage removal
```

## Dependency Graph

```
Sleep       — standalone (needs BasicNeeds.energy)
Explore     — standalone (uses Path)
Flee        — standalone (uses Path)
Mine        — standalone (needs Object::stone_deposit/ore_deposit, Item::stone/ore)
Fish        — standalone (needs Item::fish)
Hunt        — standalone (needs Item::meat)
Craft       — depends on: Mine (stone), Hunt (meat), Fish (fish) for ingredients
Talk        — depends on: ConversationManager context
Trade       — depends on: ConversationManager context, Item::from_string()
Build       — depends on: Mine (stone), Craft (wood/plank) for materials
```

## ActionParams Reference (`src/ai/action_id.h`)

```cpp
struct ActionParams {
    std::string target_type;        // "stone_deposit", "bunny", "berry_bush"
    entt::entity target_entity;     // specific entity target
    std::string target_name;        // "Entity#42" (Talk/Trade)
    std::string message;            // dialogue text (Talk)
    std::string resource;           // "wood", "stone", "ore" (Mine)
    int amount;                     // quantity
    std::string recipe;             // "wooden_sword" (Craft)
    std::string structure;          // "house", "wall" (Build)
    std::string direction;          // "north", "south" (Explore/Flee)
    float duration;                 // seconds (Sleep)
    TradeOffer trade_offer;         // give/want items (Trade)
};
```
