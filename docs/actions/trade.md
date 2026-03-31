# Trade Action

## Overview

Exchange resources with another entity. The entity pathfinds to the trade partner, proposes a trade via `ConversationManager`, waits for a simulated negotiation period, then executes the item exchange if the partner has the requested items. Uses a simplified auto-accept model.

## ActionID

`ActionID::Trade`

## Parameters

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `target_name` | `std::string` | `""` | Trade partner name |
| `target_entity` | `entt::entity` | `entt::null` | Direct entity reference |
| `trade_offer` | `TradeOffer` | — | What to give and what to want |

### TradeOffer Structure (`src/ai/action_id.h`)

Already defined:

```cpp
struct TradeOffer {
    std::string give_item;      // "berry"
    int give_amount = 1;
    std::string want_item;      // "wood"
    int want_amount = 1;
    bool accept = false;
};
```

## Infrastructure Changes

### Item String Helpers

Uses `Item::from_string()` and `Item::to_string()` (introduced by Craft action) to convert `trade_offer.give_item`/`want_item` strings to `Item::ID` values.

### ConversationManager Access

Same as Talk action — uses `this->reg.ctx<ConversationManager>()`.

### Trade Acceptance Model

Simplified for Phase 6: auto-accept if:
1. Target entity has the wanted items in their Storage
2. This entity has the offered items in their Storage
3. Offer is "fair" — `give_amount >= want_amount * 0.5` (rough heuristic)

Future enhancement: LLM-driven negotiation where the target entity's AI evaluates the offer.

## Stages

```
machine_
    .stage("finding trade partner", [this](double) -> StageStatus {
        // Resolve target (same pattern as TalkAction)
        if (this->params.target_entity != entt::null && this->reg.valid(this->params.target_entity)) {
            target_ = this->params.target_entity;
        } else if (!this->params.target_name.empty()) {
            target_ = resolve_target(this->reg, this->params.target_name);
        } else {
            this->failure_reason = "no trade partner specified";
            return StageStatus::Failed;
        }

        if (target_ == entt::null || !this->reg.all_of<Storage>(target_)) {
            this->failure_reason = "trade partner not found";
            return StageStatus::Failed;
        }

        // Validate we have items to give
        Item::ID give_id = Item::from_string(this->params.trade_offer.give_item);
        auto& my_storage = this->reg.get<Storage>(this->entity);
        if (my_storage.amount(give_id) < this->params.trade_offer.give_amount) {
            this->failure_reason = "don't have enough " + this->params.trade_offer.give_item + " to trade";
            return StageStatus::Failed;
        }

        // Pathfind to target
        auto& map = this->reg.ctx<MapManager>();
        auto my_pos = this->reg.get<Position>(this->entity);
        auto target_pos = this->reg.get<Position>(target_);

        auto path = map.pathfind(my_pos, [&](glm::vec2 pos) {
            return glm::distance(pos, glm::vec2(target_pos.x, target_pos.y)) < 3.0f;
        });

        if (path.tiles.empty()) {
            this->failure_reason = "can't reach trade partner";
            return StageStatus::Failed;
        }

        this->reg.emplace<Path>(this->entity, std::move(path));
        return StageStatus::Complete;
    })
    .stage("approaching",
        stage_primitives::wait_until_removed<Path>(reg, entity))
    .stage("proposing trade", [this](double) -> StageStatus {
        auto& conv_mgr = this->reg.ctx<ConversationManager>();

        conv_ = conv_mgr.find_conversation(this->entity, target_);
        if (!conv_) {
            conv_ = conv_mgr.start_conversation(this->entity, target_);
        }

        // Record trade proposal as conversation message
        std::string offer_msg = "I'd like to trade " +
            std::to_string(this->params.trade_offer.give_amount) + " " +
            this->params.trade_offer.give_item + " for " +
            std::to_string(this->params.trade_offer.want_amount) + " " +
            this->params.trade_offer.want_item;

        conv_mgr.add_message(conv_, this->entity, offer_msg);

        // Record in both memories
        if (auto* mem = this->reg.try_get<AIMemory>(this->entity)) {
            mem->add_event("trade_proposed", "Proposed trade: " + offer_msg);
        }
        if (auto* their_mem = this->reg.try_get<AIMemory>(target_)) {
            their_mem->add_message("Trade offer: " + offer_msg);
        }

        return StageStatus::Complete;
    })
    .stage("waiting for response",
        stage_primitives::wait_for_seconds(10.0))
    .stage("exchanging items", [this](double) -> StageStatus {
        Item::ID give_id = Item::from_string(this->params.trade_offer.give_item);
        Item::ID want_id = Item::from_string(this->params.trade_offer.want_item);
        int give_amt = this->params.trade_offer.give_amount;
        int want_amt = this->params.trade_offer.want_amount;

        auto& my_storage = this->reg.get<Storage>(this->entity);
        auto& their_storage = this->reg.get<Storage>(target_);

        // Check if trade is possible
        if (my_storage.amount(give_id) < give_amt) {
            this->failure_reason = "no longer have enough " + this->params.trade_offer.give_item;
            return StageStatus::Failed;
        }
        if (their_storage.amount(want_id) < want_amt) {
            this->failure_reason = "partner doesn't have enough " + this->params.trade_offer.want_item;
            return StageStatus::Failed;
        }

        // Execute trade
        my_storage.remove(Item(give_id, give_amt));
        my_storage.add(Item(want_id, want_amt));
        their_storage.remove(Item(want_id, want_amt));
        their_storage.add(Item(give_id, give_amt));

        // Record in memories
        std::string trade_desc = "Traded " + std::to_string(give_amt) + " " +
            this->params.trade_offer.give_item + " for " +
            std::to_string(want_amt) + " " + this->params.trade_offer.want_item;

        if (auto* mem = this->reg.try_get<AIMemory>(this->entity)) {
            mem->add_event("traded", trade_desc);
        }
        if (auto* their_mem = this->reg.try_get<AIMemory>(target_)) {
            their_mem->add_event("traded", "Received trade: " + trade_desc);
        }

        // Conclude conversation
        auto& conv_mgr = this->reg.ctx<ConversationManager>();
        conv_mgr.add_message(conv_, target_, "Trade accepted.");
        conv_mgr.conclude_conversation(conv_);

        return StageStatus::Complete;
    });
```

## Action Header

```cpp
#ifndef TRADE_ACTION_H
#define TRADE_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>

namespace dy {

class Conversation;

class TradeAction : public ActionBase<TradeAction> {
public:
    TradeAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});
    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    entt::entity target_ = entt::null;
    Conversation* conv_ = nullptr;
    ActionStageMachine machine_;
};

}

#endif
```

## Action Implementation

```cpp
std::unique_ptr<Action> TradeAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();
    StageStatus status = machine_.tick(dt);
    if (status == StageStatus::Failed) return nullptr;
    return (status == StageStatus::Continue) ? std::move(self) : nullptr;
}

std::string TradeAction::describe() const {
    return "Trade: " + machine_.current_stage_name();
}
```

## Prerequisites

Has items to give:

```cpp
// In action_registry_init.cpp:
{
    .description = "needs items to trade",
    .is_satisfied = [](auto& r, auto e) {
        // Can't validate specific items without knowing the trade_offer params
        // Just check entity has a Storage with something in it
        if (!r.template all_of<Storage>(e)) return false;
        // Any items at all?
        auto& s = r.template get<Storage>(e);
        return s.amount(Item::berry) > 0 || s.amount(Item::wood) > 0
            || s.amount(Item::stone) > 0 || s.amount(Item::ore) > 0
            || s.amount(Item::fish) > 0 || s.amount(Item::meat) > 0;
    },
    .resolve_action = [](auto& r, auto e) { return ActionID::Harvest; }
}
```

## Failure Conditions

- `"no trade partner specified"` — no target_name or target_entity provided
- `"trade partner not found"` — entity doesn't exist or has no Storage
- `"don't have enough [item] to trade"` — insufficient items to give
- `"can't reach trade partner"` — pathfinding failed
- `"no longer have enough [item]"` — items consumed between proposal and exchange
- `"partner doesn't have enough [item]"` — target lacks wanted items

## Memory Events

**Proposer** (this entity):
- `"trade_proposed"`: `"Proposed trade: [give] for [want]"`
- `"traded"`: `"Traded [give_amount] [give_item] for [want_amount] [want_item]"`

**Partner** (target entity):
- Receives unread message about trade offer
- `"traded"`: `"Received trade: [details]"`

## Example LLM JSON

```json
{
  "action": "Trade",
  "target_name": "Entity#42",
  "trade_offer": {
    "give_item": "berry",
    "give_amount": 10,
    "want_item": "wood",
    "want_amount": 3
  },
  "thought": "I have too many berries. Entity#42 might have wood to trade.",
  "speech": "Hey, want to trade?"
}
```
