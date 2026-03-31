# Talk Action

## Overview

Converse with another entity. The entity pathfinds to the target, starts or joins a conversation via `ConversationManager`, delivers a message, then **directly triggers an LLM response from the target** — which is displayed as a speech bubble and recorded in both entities' `AIMemory`. No new ECS system needed.

## ActionID

`ActionID::Talk`

## Parameters

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `target_name` | `std::string` | `""` | Name of entity to talk to (e.g., `"Entity#42"`) |
| `target_entity` | `entt::entity` | `entt::null` | Direct entity reference (overrides target_name) |
| `message` | `std::string` | `""` | What to say |

## Infrastructure Changes

### ConversationManager & LLMManager Access

Both are already in the registry context:

```cpp
auto& conv_mgr = this->reg.ctx<ConversationManager>();
auto* llm      = this->reg.ctx<LLMManager*>();   // may be nullptr
```

### SpeechBubble Component

The target's reply is shown via the existing `SpeechBubble` component (see Phase 5). Emplace it on `target_`:

```cpp
this->reg.emplace_or_replace<SpeechBubble>(target_, reply_text);
```

### Entity Name Resolution

```cpp
entt::entity resolve_target(entt::registry& reg, const std::string& target_name) {
    if (target_name.starts_with("Entity#")) {
        auto id = static_cast<entt::entity>(std::stoul(target_name.substr(7)));
        if (reg.valid(id)) return id;
    }
    auto view = reg.view<Personality>();
    for (auto [e, p] : view.each()) {
        if (p.archetype == target_name) return e;
    }
    return entt::null;
}
```

### Conversation Response Prompt

A focused prompt — not a full decision prompt. Build inline in the action:

```cpp
std::string build_reply_prompt(entt::registry& reg, entt::entity target,
                               const std::string& speaker_name,
                               const std::string& message) {
    std::ostringstream oss;
    oss << speaker_name << " just said to you: \"" << message << "\"\n\n";

    if (reg.all_of<AIMemory>(target)) {
        const auto& mem = reg.get<AIMemory>(target);
        if (!mem.events.empty()) {
            oss << "Recent memory:\n";
            int shown = 0;
            for (int i = (int)mem.events.size() - 1; i >= 0 && shown < 3; --i, ++shown)
                oss << "- " << mem.events[i].description << "\n";
            oss << "\n";
        }
    }

    oss << "Respond naturally, in character. Return JSON:\n"
        << "{ \"thought\": \"your internal reaction\", \"reply\": \"what you say\" }\n";
    return oss.str();
}
```

Response parsing — look for `"reply"` in the JSON object, fall back to raw text:

```cpp
std::string parse_reply(const std::string& raw) {
    // Try JSON parse for "reply" key
    auto start = raw.find("\"reply\"");
    if (start != std::string::npos) {
        auto q1 = raw.find('"', start + 7);
        if (q1 != std::string::npos) {
            auto q2 = raw.find('"', q1 + 1);
            if (q2 != std::string::npos)
                return raw.substr(q1 + 1, q2 - q1 - 1);
        }
    }
    // Fallback: return first non-empty line
    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line))
        if (!line.empty()) return line;
    return "...";
}
```

## Stages

```
machine_
    // ── 1. Resolve target + pathfind ────────────────────────────────────
    .stage("finding partner", [this](double) -> StageStatus {
        if (this->params.target_entity != entt::null
            && this->reg.valid(this->params.target_entity)) {
            target_ = this->params.target_entity;
        } else if (!this->params.target_name.empty()) {
            target_ = resolve_target(this->reg, this->params.target_name);
        } else {
            // Nearest human fallback
            auto my_pos = this->reg.get<Position>(this->entity);
            float closest = 999999.0f;
            for (auto [e, obj, pos] : this->reg.view<Object, Position>().each()) {
                if (e == this->entity || obj.id != Object::human) continue;
                float d = glm::distance(glm::vec2(my_pos.x, my_pos.y),
                                        glm::vec2(pos.x, pos.y));
                if (d < closest) { closest = d; target_ = e; }
            }
        }
        if (target_ == entt::null) {
            this->failure_reason = "no one to talk to";
            return StageStatus::Failed;
        }
        auto& map       = this->reg.ctx<MapManager>();
        auto my_pos     = this->reg.get<Position>(this->entity);
        auto target_pos = this->reg.get<Position>(target_);
        auto path = map.pathfind(my_pos, [&](glm::vec2 p) {
            return glm::distance(p, glm::vec2(target_pos.x, target_pos.y)) < 3.0f;
        });
        if (path.tiles.empty()) {
            this->failure_reason = "can't reach conversation partner";
            return StageStatus::Failed;
        }
        this->reg.emplace<Path>(this->entity, std::move(path));
        return StageStatus::Complete;
    })

    // ── 2. Walk to target ────────────────────────────────────────────────
    .stage("approaching",
        stage_primitives::wait_until_removed<Path>(reg, entity))

    // ── 3. Open / join conversation ──────────────────────────────────────
    .stage("starting conversation", [this](double) -> StageStatus {
        auto& conv_mgr = this->reg.ctx<ConversationManager>();
        conv_ = conv_mgr.find_conversation(this->entity, target_);
        if (!conv_) conv_ = conv_mgr.start_conversation(this->entity, target_);
        if (!conv_) {
            this->failure_reason = "couldn't start conversation";
            return StageStatus::Failed;
        }
        return StageStatus::Complete;
    })

    // ── 4. Deliver speaker's message ─────────────────────────────────────
    .stage("talking", [this](double) -> StageStatus {
        auto& conv_mgr = this->reg.ctx<ConversationManager>();
        std::string msg = this->params.message.empty() ? "Hello!" : this->params.message;
        conv_mgr.add_message(conv_, this->entity, msg);

        // Record in speaker's own memory
        if (auto* mem = this->reg.try_get<AIMemory>(this->entity))
            mem->record_event("talked", "Said \"" + msg + "\" to another entity", true);

        // Queue in target's unread messages for the reply prompt
        if (auto* mem = this->reg.try_get<AIMemory>(target_))
            mem->unread_messages.push_back(msg);

        // Submit LLM request on behalf of the target entity
        auto* llm = this->reg.ctx<LLMManager*>();
        if (llm && llm->is_available()) {
            // Build speaker name from entity id
            std::string speaker = "Entity#" + std::to_string((uint32_t)this->entity);
            std::string user_prompt   = build_reply_prompt(this->reg, target_, speaker, msg);
            std::string system_prompt = "";
            if (this->reg.all_of<Personality>(target_)) {
                PromptBuilder pb;
                system_prompt = pb.build_system_prompt(&this->reg.get<Personality>(target_));
            }
            response_request_id_ = llm->submit_request(user_prompt, system_prompt);
        }
        // If LLM unavailable, response_request_id_ stays 0 — skip wait stage
        return StageStatus::Complete;
    })

    // ── 5. Wait for target's LLM reply ───────────────────────────────────
    .stage("awaiting response", [this](double dt) -> StageStatus {
        // Skip if no request was submitted (LLM unavailable)
        if (response_request_id_ == 0) return StageStatus::Complete;

        response_wait_elapsed_ += (float)dt;
        if (response_wait_elapsed_ > response_timeout_) {
            // Timed out — continue without a reply
            response_request_id_ = 0;
            return StageStatus::Complete;
        }

        auto* llm = this->reg.ctx<LLMManager*>();
        if (!llm) return StageStatus::Complete;

        LLMResponse resp;
        if (!llm->try_get_result(response_request_id_, resp))
            return StageStatus::Continue;  // still pending

        // Parse reply text
        std::string reply = parse_reply(resp.raw_text);

        // Show as speech bubble on target
        this->reg.emplace_or_replace<SpeechBubble>(target_, reply);

        // Record in conversation
        auto& conv_mgr = this->reg.ctx<ConversationManager>();
        conv_mgr.add_message(conv_, target_, reply);

        // Record in both memories
        if (auto* mem = this->reg.try_get<AIMemory>(target_))
            mem->record_event("replied", "Responded: \"" + reply + "\"", true);
        if (auto* mem = this->reg.try_get<AIMemory>(this->entity))
            mem->record_event("heard", "Heard reply: \"" + reply + "\"", false);

        return StageStatus::Complete;
    })

    // ── 6. Close conversation ────────────────────────────────────────────
    .stage("concluding", [this](double) -> StageStatus {
        auto& conv_mgr = this->reg.ctx<ConversationManager>();
        conv_mgr.conclude_conversation(conv_);
        return StageStatus::Complete;
    });
```

## Action Header

```cpp
#ifndef TALK_ACTION_H
#define TALK_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>
#include <llm/llm_manager.h>

namespace dy {

class Conversation;  // forward declare

class TalkAction : public ActionBase<TalkAction> {
public:
    TalkAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});
    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    entt::entity target_              = entt::null;
    Conversation* conv_               = nullptr;
    ActionStageMachine machine_;

    uint64_t response_request_id_     = 0;
    float    response_wait_elapsed_   = 0.0f;
    static constexpr float response_timeout_ = 15.0f;  // seconds
};

}  // namespace dy

#endif
```

## Action Implementation

```cpp
std::unique_ptr<Action> TalkAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();
    StageStatus status = machine_.tick(dt);
    if (status == StageStatus::Failed) return nullptr;
    return (status == StageStatus::Continue) ? std::move(self) : nullptr;
}

std::string TalkAction::describe() const {
    return "Talk: " + machine_.current_stage_name();
}
```

## Prerequisites

None. Target existence is validated in stage 1.

## Failure Conditions

| Reason | Cause |
|--------|-------|
| `"no one to talk to"` | Target not found or invalid |
| `"can't reach conversation partner"` | Pathfinding failed |
| `"couldn't start conversation"` | ConversationManager returned null |

## Graceful Degradation

- **LLM unavailable**: `response_request_id_` stays 0; "awaiting response" completes immediately. Conversation is recorded but no reply is shown.
- **LLM timeout** (> 15 s): same as above — continues to conclude stage.

## Memory Events

| Entity | Type | Description |
|--------|------|-------------|
| Speaker | `"talked"` | `"Said '[msg]' to another entity"` |
| Target (LLM) | `"replied"` | `"Responded: '[reply]'"` |
| Speaker | `"heard"` | `"Heard reply: '[reply]'"` |

## Example LLM JSON

```json
{
  "action": "Talk",
  "target_name": "Entity#42",
  "message": "Hello! How are you doing today?",
  "thought": "That entity looks friendly. I should introduce myself.",
  "speech": "Hello there!"
}
```

**Target's reply prompt** (submitted by TalkAction on target's behalf):

```
Entity#7 just said to you: "Hello! How are you doing today?"

Recent memory:
- Harvested wood near the river
- Ate berries

Respond naturally, in character. Return JSON:
{ "thought": "your internal reaction", "reply": "what you say" }
```

**Expected target LLM response**:

```json
{ "thought": "A stranger greeting me while I work. I'll be polite.", "reply": "Oh, hello! Doing well enough — just keeping busy. You?" }
```
