# Talk Action & Dialogue System

## Overview

The conversation system spans three layers: `TalkAction` (the action entity), `ConversationManager` (persistent state), and LLM integration (prompt building + response parsing). The layers are loosely coupled but have structural mismatches that produce the observed broken conversation behavior.

---

## Current Implementation

### TalkAction (`src/ai/actions/talk_action.h/.cpp`)

A single execution of `TalkAction` represents **one message exchange only** — not a full conversation. It uses a 4-stage `ActionStageMachine`:

| Stage | What Happens |
|---|---|
| `finding partner` | Resolve target by name, entity handle, or nearest human. Pathfind to within 3.0 units. |
| `approaching` | Wait for `Path` component removal (movement complete). |
| `starting conversation` | Find or create a `Conversation` in `ConversationManager`. |
| `conversing` | Send one message. If LLM available, submit a reply request (15s timeout). Receive one reply. Return **Complete**. |

After returning `Complete`, control returns to `AISys`. The AI must then re-decide and choose `Talk` again to continue the conversation. Each round-trip through the decision loop is one exchange.

**LLM reply prompt** (`submit_reply()` in `talk_action.cpp`): Sends the other party's message and **the last 3 memory events** from the responder's `AIMemory`. The conversation history is **not included** in this prompt.

**Memory recording**: Both entities record `"said: <text>"` and `"heard: <text>"` events in `AIMemory` for every exchange.

### ConversationManager (`src/ai/conversation/conversation_manager.h/.cpp`)

Owns all `Conversation` objects. Provides:

- `start_conversation(a, b)` — creates a new `Conversation` between two entities
- `find_conversation(a, b)` — finds first active conversation between them
- `add_message(conv, sender, text, name)` — appends a `ConversationMessage`
- `conclude_conversation(conv)` — sets state to `Concluded`
- `tick(reg, dt)` — removes conversations where either participant entity is invalid

`CONVERSATION_TIMEOUT = 300.0f` is defined but **never used** — the `tick()` method does not enforce it.

### Conversation Data (`src/ai/conversation/conversation.h`)

```cpp
struct Conversation {
    entt::entity participant_a, participant_b;
    std::vector<ConversationMessage> messages;
    ConversationState state;  // Active | Concluded | Waiting (Waiting never used)
    std::string started_at;
    std::string last_message_at;
};
```

`ConversationState::Waiting` is defined but never assigned anywhere.

### LLM Decision Loop (`src/ai/ai.cpp`, `src/llm/prompt_builder.cpp`)

The **main decision prompt** includes the active conversation via `build_context()`:
- Partner name
- Last **6 messages** from conversation history
- Instruction: *"Choose Talk to continue it, or another action to end it."*

After the LLM responds, `AISys::poll_llm_results()` checks: if the first chosen action is not `Talk`, all active conversations for that entity are immediately concluded via `conclude_conversation()`.

No summary is generated at this point — conversations end silently.

---

## Problems

### 1. Single-Exchange Architecture Creates Fragile Loops

Every `TalkAction` does one send + one receive, then exits. For a conversation to continue, the entity must re-run through the full AI decision cycle: send LLM request, wait for response, parse, queue. Any disruption in that cycle — LLM timeout, entity needs (hunger, energy), scoring pressure — silently kills the conversation.

Two entities having a back-and-forth conversation requires **both** to keep independently choosing `Talk` every tick cycle. There is no coordination.

### 2. Both NPCs Initiate Simultaneously

Both entities in proximity can independently create `TalkAction` targeting each other at the same time. Entity A creates a TalkAction targeting B; Entity B creates a TalkAction targeting A. Both enter the "approaching" stage and pathfind toward each other, causing them to swap positions. Both then independently call `start_conversation()` or `find_conversation()` and may create two separate conversations, or both latch onto the same one and both try to be the speaker.

There is no initiation lock, no "I am already being approached" check, and no role assignment (initiator vs. responder).

### 3. Position Swapping During Approach

Entity A pathfinds to within 3.0 units of B. Entity B pathfinds to within 3.0 units of A. Since both are moving toward each other simultaneously, they often overshoot and end up on opposite sides of each other's starting positions.

### 4. Reply Prompt Has Almost No Conversation Context

`submit_reply()` builds the responder's prompt with only:
- The incoming message text
- Last 3 events from the responder's `AIMemory`

The **conversation history is not passed** to the entity generating the reply. The responder has no memory of what was said before the current message. This is why NPCs repeat themselves — from the LLM's perspective, every message is the first in an isolated exchange.

### 5. No Meaningful End-Conversation Action

The only way a conversation ends is if the LLM picks any action other than `Talk`. This is an implicit, accidental termination. There is no `EndConversation` or `Farewell` action that:
- Signals conversational closure
- Triggers a summary generation
- Provides a natural narrative exit ("It was good talking to you, goodbye")

The LLM is never instructed on *when* or *how* to end conversations — only that not choosing `Talk` will end them.

### 6. Action Selection and Dialogue Are Conflated

The main decision prompt asks the LLM to simultaneously:
- Choose an action (Eat, Wander, Talk, etc.)
- Write dialogue (via `dialogue` field in response)

For conversational turns, these are different cognitive tasks. A dialogue response should be guided by conversational context, not action selection pressure. When the entity is hungry or tired, the scoring system biases away from `Talk`, cutting conversations off mid-sentence regardless of narrative state.

### 7. No Conversation Summary in AIMemory

When a conversation concludes, nothing is summarized. `AIMemory` accumulates raw `"said: ..."` and `"heard: ..."` events for every single exchange. A ten-message conversation produces twenty memory events, all with equal weight and no distilled meaning.

Future LLM prompts include recent memory events, so after a long conversation the memory log is dominated by raw dialogue fragments rather than meaningful facts ("learned that Erik is a trader from the north").

### 8. Conversation History in Decision Prompt Is Often Stale

The main decision prompt shows the last 6 messages. But since each `TalkAction` is one exchange and the decision prompt is only built at the *start* of each decision cycle, the LLM may be working from a snapshot that is 1-2 exchanges behind.

---

## Potential Solutions

### A. Give TalkAction Its Own Dialogue Loop

Instead of one exchange per execution, `TalkAction` should own the full conversation loop internally. The stage machine should loop within the `conversing` stage — send, receive, send, receive — until an internal termination condition is met. This removes the dependency on the AI decision loop for every exchange.

Termination conditions could include:
- A special `[END_CONVERSATION]` token in the LLM reply
- A maximum exchange count (e.g., 8 messages)
- Entity urgency exceeding a threshold (very hungry, very tired)

### B. Assign Initiator/Responder Roles

When A initiates a conversation with B, B should receive a flag (`in_conversation_as_responder = true`) that suppresses new outgoing `TalkAction` creation. This prevents the simultaneous initiation race. The responder reacts in place without pathfinding. Only the initiator moves.

### C. Separate Dialogue Prompt from Decision Prompt

Create a dedicated `build_dialogue_prompt()` that:
- Includes the **full conversation history** (not just last 6 messages)
- Is framed as "continue this dialogue" not "choose an action"
- Takes personality and current emotional state as tone modifiers
- Asks only for a reply string, not an action selection

The main decision prompt remains for action selection and should include a brief conversation summary ("currently in conversation with Erik about trade") rather than raw message history.

### D. Add an EndConversation Action

Register an explicit `EndConversation` action (or `Farewell`) that:
- Generates a closing dialogue line ("I should go, good talking with you")
- Triggers `ConversationManager::conclude_conversation()`
- Calls a `summarize_conversation()` function before concluding

The LLM dialogue prompt can include an instruction like: *"Reply with your next line. If the conversation has reached a natural conclusion, end your reply with [DONE]."*

### E. Store Conversation Summary in AIMemory

When `conclude_conversation()` is called, submit a summarization LLM request with the full message history and ask for 1-3 sentences of key facts. Store the result as a single `AIMemory` event of type `"conversation_summary"`. Discard or archive the raw said/heard events from that conversation.

This keeps the memory log meaningful and compact. Future decision prompts will see "Spoke with Erik. Learned he is a trader from the north looking for iron. He mentioned the mine to the east." instead of ten raw dialogue fragments.

### F. Pass Full Conversation History to Reply Prompts

`submit_reply()` should include the full `Conversation::messages` history, not just 3 recent memory events. This is the primary fix for repetitive dialogue — the responder needs to know what has already been said to avoid repeating it.

---

## Connections to Other Systems

| System | Coupling | Notes |
|---|---|---|
| `AISys` | Tight | Polls LLM results and concludes conversations on non-Talk decision |
| `PromptBuilder` | Moderate | Builds context with last 6 messages; no dedicated dialogue prompt |
| `LLMManager` | Moderate | Used for both main decisions and `submit_reply()` on same thread pool |
| `AIMemory` | Loose | Records raw said/heard events; no summarization hook |
| `ActionStageMachine` | Internal | Drives the 4-stage flow; terminates after one exchange |
| `SpeechBubble` | Loose | Attached per message; cleared automatically by duration |
| `MapManager / Path` | Loose | Used only during approach stage; no re-approach logic |
| `PersonalityInspector` | Read-only | Displays conversation history in UI |
| `EntityIdentity` | Read-only | Provides name and personality type for reply prompt system prompt |

---

*Files: `src/ai/actions/talk_action.h/.cpp`, `src/ai/conversation/conversation.h`, `src/ai/conversation/conversation_manager.h/.cpp`, `src/llm/prompt_builder.h/.cpp`, `src/ai/ai.h/.cpp`*
