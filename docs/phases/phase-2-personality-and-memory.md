# Phase 2: Personality & Memory

**Goal**: Create personality generation and AI memory systems for tracking entity state, past conversations, and events.

**Scope**: No LLM calls yet — this phase establishes the data structures that will be consumed by LLM prompts in Phase 3.

**Estimated Scope**: ~800 lines of new code

**Completion Criteria**: Personalities are generated at entity spawn, serialized/deserialized via Cereal, and tracked persistently. Conversations can be created and tracked.

---

## Phase 1 Completion Context

Phase 1 is complete. The following were delivered and are available for Phase 2 to build on:

| Delivered | File | Notes |
|-----------|------|-------|
| `ActionID` enum | `src/ai/action_id.h` | 13 action types + `ActionParams` struct |
| `ActionBase<T>` CRTP | `src/ai/action_base.h` | Dispatches `act(self, dt)` → `act_impl(self, dt)` |
| `ActionRegistry` singleton | `src/ai/action_registry.h/.cpp` | Factory + descriptor lookup |
| `PrerequisiteResolver` | `src/ai/prerequisite_resolver.h/.cpp` | Recursive with cycle detection |
| **`ActionStageMachine`** | **`src/ai/action_stage.h`** | **Named lambda stages — new in Phase 1** |
| **`Action::describe()`** | **`src/ai/actions/action.h`** | **Returns current stage name for speech bubbles** |
| **`Action::failure_reason`** | **`src/ai/actions/action.h`** | **`std::string`, set before returning `nullptr` on failure** |
| `HarvestAction` (refactored) | `src/ai/actions/harvest_action.*` | Reference: 4 named stages |
| `EatAction` (refactored) | `src/ai/actions/eat_action.*` | Reference: 2 named stages |
| `dt` in action loop | `src/ai/ai.cpp` | `act(self, dt)` — enables timer-based waits |

**Key integration points for Phase 2**:
- `action->describe()` — call this when building AIMemory event descriptions to include human-readable stage context
- `action->failure_reason` — must be captured *before* the action's `unique_ptr` is destroyed; see §2.3 below
- `stage_primitives::wait_for_seconds(secs)` — enables `SleepAction` implementation in Phase 6

---

## Context: Why This Phase Matters

The LLM system needs to know:
1. **Who is this entity?** → Personality traits
2. **What has happened to them?** → Past events in AIMemory
3. **What conversations are they having?** → ConversationManager

Phase 2 builds these subsystems independently of the LLM. Phase 3 will bind them together.

---

## 1. Personality System

### 1.1 Personality Component

Create `src/ai/personality/personality.h`:

```cpp
#pragma once
#include <string>
#include <vector>
#include <cereal/cereal.hpp>

struct PersonalityTrait {
    std::string name;           // "friendliness", "industriousness", "curiosity"
    float value;                // 0.0 to 1.0
    std::string description;    // "Very friendly and outgoing"

    template<class Archive>
    void serialize(Archive& ar) {
        ar(name, value, description);
    }
};

struct Personality {
    std::string archetype;      // "explorer", "builder", "socialite", "hermit"
    std::string speech_style;   // "formal", "casual", "poetic", "blunt"
    std::string motivation;     // "wealth", "knowledge", "relationships", "comfort"

    std::vector<PersonalityTrait> traits;

    // Generated unique identifier
    uint32_t personality_seed = 0;

    // Full personality description for LLM prompts
    std::string to_prompt_text() const;

    template<class Archive>
    void serialize(Archive& ar) {
        ar(archetype, speech_style, motivation, traits, personality_seed);
    }
};
```

### 1.2 Personality Generator

Create `src/ai/personality/personality_generator.h` and `.cpp`:

```cpp
// personality_generator.h
#pragma once
#include "personality.h"
#include <random>

class PersonalityGenerator {
public:
    // Generate a random personality
    Personality generate_random();

    // Generate from seed (for reproducibility)
    Personality generate_from_seed(uint32_t seed);

private:
    std::mt19937 rng;

    std::string pick_archetype();
    std::string pick_speech_style();
    std::string pick_motivation();
    PersonalityTrait generate_trait();
};
```

```cpp
// personality_generator.cpp
#include "personality_generator.h"
#include <chrono>

Personality PersonalityGenerator::generate_random() {
    uint32_t seed = static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());
    return generate_from_seed(seed);
}

Personality PersonalityGenerator::generate_from_seed(uint32_t seed) {
    rng.seed(seed);

    Personality p;
    p.personality_seed = seed;
    p.archetype = pick_archetype();
    p.speech_style = pick_speech_style();
    p.motivation = pick_motivation();

    // Generate 4-6 traits
    std::uniform_int_distribution<int> trait_count(4, 6);
    for (int i = 0; i < trait_count(rng); ++i) {
        p.traits.push_back(generate_trait());
    }

    return p;
}

std::string PersonalityGenerator::pick_archetype() {
    static const std::vector<std::string> archetypes = {
        "explorer", "builder", "socialite", "hermit", "scholar", "artist", "warrior", "merchant"
    };
    std::uniform_int_distribution<size_t> dist(0, archetypes.size() - 1);
    return archetypes[dist(rng)];
}

std::string PersonalityGenerator::pick_speech_style() {
    static const std::vector<std::string> styles = {
        "formal", "casual", "poetic", "blunt", "verbose", "minimal"
    };
    std::uniform_int_distribution<size_t> dist(0, styles.size() - 1);
    return styles[dist(rng)];
}

std::string PersonalityGenerator::pick_motivation() {
    static const std::vector<std::string> motivations = {
        "wealth", "knowledge", "relationships", "comfort", "freedom", "power", "legacy"
    };
    std::uniform_int_distribution<size_t> dist(0, motivations.size() - 1);
    return motivations[dist(rng)];
}

PersonalityTrait PersonalityGenerator::generate_trait() {
    static const std::vector<std::string> trait_names = {
        "friendliness", "industriousness", "curiosity", "bravery", "generosity",
        "ambition", "loyalty", "creativity", "patience", "humor"
    };
    std::uniform_int_distribution<size_t> name_dist(0, trait_names.size() - 1);
    std::uniform_real_distribution<float> value_dist(0.2f, 0.9f);

    std::string name = trait_names[name_dist(rng)];
    float value = value_dist(rng);

    return PersonalityTrait{
        .name = name,
        .value = value,
        .description = name + " level " + std::to_string(static_cast<int>(value * 10))
    };
}
```

### 1.3 Personality Serialization

Add to `Personality` class in `.cpp`:

```cpp
std::string Personality::to_prompt_text() const {
    std::string result;
    result += "Archetype: " + archetype + "\n";
    result += "Speech Style: " + speech_style + "\n";
    result += "Primary Motivation: " + motivation + "\n";
    result += "Traits:\n";
    for (const auto& trait : traits) {
        result += "  - " + trait.name + ": " + std::to_string(trait.value) + "\n";
    }
    return result;
}
```

### 1.4 Register Personality Component

In `src/logic/components/` add personality as a component in the registry (handled in Phase 4 when integrating into game loop).

---

## 2. AI Memory System

### 2.1 AIMemory Component

Create `src/ai/speech/ai_memory.h`:

```cpp
#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <cereal/cereal.hpp>

struct MemoryEvent {
    std::string event_type;     // "harvested", "ate", "traded", "talked", "failed_action"
    std::string description;    // "Harvested 5 berries from bush near river"
    std::string timestamp;      // ISO 8601 or game-time
    bool seen_by_llm = false;   // Has this been included in an LLM prompt?

    template<class Archive>
    void serialize(Archive& ar) {
        ar(event_type, description, timestamp, seen_by_llm);
    }
};

struct AIMemory {
    std::vector<MemoryEvent> events;
    std::vector<std::string> unread_messages;  // Messages received but not yet processed by LLM

    // Mark all events as seen (called after including in LLM prompt)
    void mark_all_seen();

    // Add a new event
    void add_event(const std::string& type, const std::string& description);

    // Add a message (e.g., from conversation partner)
    void add_message(const std::string& message);

    // Clear unread messages
    void clear_unread();

    template<class Archive>
    void serialize(Archive& ar) {
        ar(events, unread_messages);
    }
};
```

### 2.2 AIMemory Implementation

Create `src/ai/speech/ai_memory.cpp`:

```cpp
#include "ai_memory.h"
#include <chrono>
#include <sstream>
#include <iomanip>

void AIMemory::mark_all_seen() {
    for (auto& event : events) {
        event.seen_by_llm = true;
    }
}

void AIMemory::add_event(const std::string& type, const std::string& description) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");

    events.push_back(MemoryEvent{
        .event_type = type,
        .description = description,
        .timestamp = oss.str(),
        .seen_by_llm = false
    });
}

void AIMemory::add_message(const std::string& message) {
    unread_messages.push_back(message);
}

void AIMemory::clear_unread() {
    unread_messages.clear();
}
```

### 2.3 Integration with Action System (Phase 1 Output)

Phase 1 added `Action::failure_reason` and `Action::describe()`. Phase 2 must wire these into AIMemory recording in `AISys`.

**Problem**: By the time `ai.action = ai.action->act(std::move(ai.action), dt)` returns `nullptr`, the action is destroyed. `failure_reason` must be captured beforehand.

**Solution**: Add `last_failure_reason` to `AIC` and capture it in the tick loop.

#### Update `src/ai/aic.h`

```cpp
class AIC {
public:
    std::unique_ptr<Action> action;
    float score = 0;
    ActionParams current_params;        // For Phase 3 LLM queue

    std::string last_failure_reason;    // Captured from action->failure_reason before destruction
    std::string last_action_description;// Captured from action->describe() before destruction
};
```

#### Update `src/ai/ai.cpp` tick loop

```cpp
view.each([&](const auto entity, auto& ai) {

    if (!ai.action || ai.action->interruptible) {
        decide(entity, ai);
    }

    // Capture describe/failure before act() potentially destroys the action
    std::string current_desc = ai.action ? ai.action->describe() : "";
    bool was_active = ai.action != nullptr;

    ai.action = ai.action->act(std::move(ai.action), dt);

    // Action just completed (returned nullptr)
    if (was_active && !ai.action) {
        // Phase 2+: write to AIMemory component if present
        if (reg.all_of<AIMemory>(entity)) {
            auto& mem = reg.get<AIMemory>(entity);
            if (!ai.last_failure_reason.empty()) {
                mem.add_event("failed_action", ai.last_failure_reason);
            } else if (!current_desc.empty()) {
                mem.add_event("completed_action", current_desc);
            }
        }
        ai.last_failure_reason.clear();
    }

    // Capture failure_reason on same tick it's set (before next act() call)
    if (ai.action && !ai.action->failure_reason.empty()) {
        ai.last_failure_reason = ai.action->failure_reason;
    }

});
```

> **Note**: The `AIMemory` component is added to entities during Phase 2 initialization. The `if (reg.all_of<AIMemory>(entity))` guard means this code compiles and runs safely before/without Phase 2 being fully integrated.

---

## 3. Conversation System

### 3.1 Conversation Structures

Create `src/ai/conversation/conversation.h`:

```cpp
#pragma once
#include <string>
#include <vector>
#include <entt/entt.hpp>
#include <cereal/cereal.hpp>

enum class ConversationState {
    Active,
    Concluded,
    Waiting
};

struct ConversationMessage {
    entt::entity sender;        // Who said this?
    std::string sender_name;    // "Entity#42" for reference
    std::string text;
    std::string timestamp;      // When was it said?
    bool read = false;          // Has the recipient read this?

    template<class Archive>
    void serialize(Archive& ar) {
        ar(sender_name, text, timestamp, read);
    }
};

struct Conversation {
    entt::entity participant_a;
    entt::entity participant_b;
    std::vector<ConversationMessage> messages;
    ConversationState state = ConversationState::Active;

    // Metadata
    std::string started_at;
    std::string last_message_at;
    int max_messages = 6;       // Auto-conclude after N messages

    bool is_active() const { return state == ConversationState::Active; }
    bool is_between(entt::entity a, entt::entity b) const;
    const ConversationMessage* get_last_unread_for(entt::entity entity) const;

    template<class Archive>
    void serialize(Archive& ar) {
        ar(participant_a, participant_b, messages, state, started_at, last_message_at, max_messages);
    }
};
```

### 3.2 Conversation Manager

Create `src/ai/conversation/conversation_manager.h` and `.cpp`:

```cpp
// conversation_manager.h
#pragma once
#include "conversation.h"
#include <vector>
#include <memory>

class ConversationManager {
public:
    ConversationManager() = default;

    // Start a new conversation between two entities
    Conversation* start_conversation(entt::entity a, entt::entity b);

    // Find active conversation between two entities
    Conversation* find_conversation(entt::entity a, entt::entity b);

    // Add a message to a conversation
    void add_message(Conversation* conv, entt::entity sender, const std::string& text);

    // Conclude a conversation
    void conclude_conversation(Conversation* conv);

    // Get all active conversations for an entity
    std::vector<Conversation*> get_active_for(entt::entity entity);

    // Tick: clean up concluded conversations, timeout expired conversations
    void tick(entt::registry& reg, float dt);

    // Get all conversations (for serialization)
    const std::vector<std::unique_ptr<Conversation>>& get_all() const { return conversations; }

private:
    std::vector<std::unique_ptr<Conversation>> conversations;
    static constexpr float CONVERSATION_TIMEOUT = 300.0f;  // 5 minutes in-game
};
```

```cpp
// conversation_manager.cpp
#include "conversation_manager.h"
#include <chrono>
#include <sstream>
#include <iomanip>

static std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

Conversation* ConversationManager::start_conversation(entt::entity a, entt::entity b) {
    auto conv = std::make_unique<Conversation>();
    conv->participant_a = a;
    conv->participant_b = b;
    conv->state = ConversationState::Active;
    conv->started_at = get_timestamp();

    auto* ptr = conv.get();
    conversations.push_back(std::move(conv));
    return ptr;
}

Conversation* ConversationManager::find_conversation(entt::entity a, entt::entity b) {
    for (auto& conv : conversations) {
        if (conv->is_between(a, b) && conv->is_active()) {
            return conv.get();
        }
    }
    return nullptr;
}

void ConversationManager::add_message(Conversation* conv, entt::entity sender, const std::string& text) {
    if (!conv) return;

    ConversationMessage msg;
    msg.sender = sender;
    msg.text = text;
    msg.timestamp = get_timestamp();
    msg.read = false;

    // Set sender name
    if (sender == conv->participant_a) {
        msg.sender_name = "Entity#" + std::to_string(static_cast<uint32_t>(sender));
    } else {
        msg.sender_name = "Entity#" + std::to_string(static_cast<uint32_t>(sender));
    }

    conv->messages.push_back(msg);
    conv->last_message_at = get_timestamp();

    if (conv->messages.size() >= conv->max_messages) {
        conclude_conversation(conv);
    }
}

void ConversationManager::conclude_conversation(Conversation* conv) {
    if (conv) {
        conv->state = ConversationState::Concluded;
    }
}

std::vector<Conversation*> ConversationManager::get_active_for(entt::entity entity) {
    std::vector<Conversation*> result;
    for (auto& conv : conversations) {
        if ((conv->participant_a == entity || conv->participant_b == entity) && conv->is_active()) {
            result.push_back(conv.get());
        }
    }
    return result;
}

void ConversationManager::tick(entt::registry& reg, float dt) {
    // Remove conversations with dead entities
    conversations.erase(
        std::remove_if(conversations.begin(), conversations.end(), [&](const auto& conv) {
            if (!reg.valid(conv->participant_a) || !reg.valid(conv->participant_b)) {
                return true;  // Remove
            }
            return false;
        }),
        conversations.end()
    );

    // Remove concluded conversations older than timeout
    // (optional: keep them for history, just mark as concluded)
}
```

---

## 4. Integration with Entity Spawning

### 4.1 Assign Personality at Spawn

When creating a human entity, generate and assign personality:

```cpp
// In game initialization or entity factory
PersonalityGenerator gen;
Personality personality = gen.generate_random();

auto entity = registry.create();
registry.emplace<Personality>(entity, personality);
registry.emplace<AIMemory>(entity);
```

### 4.2 Conversation Component

Add to `src/logic/components/`:

```cpp
struct ConversationComponent {
    // Reference to conversation in ConversationManager
    // Could be null if not in conversation
};
```

---

## 5. Serialization

### 5.1 Cereal Support

Both `Personality` and `AIMemory` have `serialize()` methods for Cereal JSON serialization.

When saving game state:

```cpp
// Save personality
std::ofstream ofs("personality.json");
cereal::JSONOutputArchive ar(ofs);
ar(personality);

// Save memory
std::ofstream ofs2("memory.json");
cereal::JSONOutputArchive ar2(ofs2);
ar2(memory);
```

Load them back:

```cpp
std::ifstream ifs("personality.json");
cereal::JSONInputArchive ar(ifs);
ar(personality);
```

---

## 6. CMakeLists.txt Updates

```cmake
target_sources(dynamical PRIVATE
    # ... existing
    src/ai/personality/personality.h
    src/ai/personality/personality_generator.h
    src/ai/personality/personality_generator.cpp

    src/ai/speech/ai_memory.h
    src/ai/speech/ai_memory.cpp

    src/ai/conversation/conversation.h
    src/ai/conversation/conversation_manager.h
    src/ai/conversation/conversation_manager.cpp
)
```

---

## 7. Testing Checklist

- [ ] PersonalityGenerator creates random personalities with all fields populated
- [ ] Same seed produces same personality (reproducibility)
- [ ] Personality serializes to JSON and deserializes correctly
- [ ] AIMemory starts empty, accepts events and messages
- [ ] AIMemory marks events as seen correctly
- [ ] ConversationManager starts conversations between entities
- [ ] Messages added to conversations are timestamped
- [ ] Conversations auto-conclude after max messages
- [ ] Invalid entities cleaned up from ConversationManager
- [ ] Conversation serialization works via Cereal
- [ ] `AIC::last_failure_reason` is populated when `EatAction` fails (no food)
- [ ] `AIC::last_failure_reason` is populated when `HarvestAction` fails (no berry bushes)
- [ ] `AIMemory` records a `"failed_action"` event when harvest fails
- [ ] `AIMemory` records a `"completed_action"` event when harvest succeeds
- [ ] Game compiles and runs without LLM components

---

## 8. Dependencies on Other Phases

- **Depends on**: Phase 1 (**complete**) — `action.h` `failure_reason` + `describe()` are required for §2.3. `AIC` modifications (§2.3) require Phase 1's `action_stage.h` to be in place.
- **Independent of**: LLM system — Phase 2 is purely data structure and glue code.
- **Used by**: Phase 3 (LLM Infrastructure) reads `Personality` + `AIMemory` to build prompts. Phase 4 reads `AIC::last_failure_reason` to include failure context in LLM re-queries.

**Phase 1 is complete — Phase 2 can begin immediately.**

---

## 9. Next Phase

**Phase 3** integrates ai-sdk-cpp and builds the LLM infrastructure. It will consume Personality and AIMemory from Phase 2 to construct prompts.

---

**Reference Sections from Global Doc:**
- Section 9: Personality System
- Section 8: Conversation System
- Section 16: File Structure (personality/, conversation/, speech/)
- Problem/Solution #9: Personality Consistency
- Problem/Solution #11: Conversation Spam
- Problem/Solution #12: Cold Start
