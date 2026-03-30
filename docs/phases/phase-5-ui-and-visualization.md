# Phase 5: UI & Visualization

**Goal**: Add ImGui overlays for entity state visualization, LLM decision monitoring, and runtime configuration panels.

**Scope**: Non-critical gameplay feature, but essential for observability and debugging.

**Estimated Scope**: ~500 lines of new code

**Completion Criteria**: Entity state panel shows action and LLM status per entity. LLM config panel allows provider/model switching at runtime. Personality inspector shows traits for a selected entity.

---

## Context: Why This Phase Matters

Without UI feedback, LLM decisions are invisible. Phase 5 adds:
1. **Entity state panel** showing current action, LLM waiting status, and last memory event
2. **LLM config panel** for runtime provider/model switching and performance toggles
3. **Personality inspector** for debugging unique entity traits

This phase is **purely visual** — no game logic changes. Can be implemented last if needed.

---

## Architecture Notes

### How ImGui works in this codebase

- `UISys::tick()` calls `ImGui::NewFrame()` each pre-tick — any system ticking after it can call `ImGui::Begin/End`
- `UIRenderSys::tick()` (post-tick) calls `ImGui::Render()` and handles Vulkan upload
- New UI systems should be added via `set->pre_add<T>()` in `game.cpp`, **after** `UISys` in the pre-tick list
- All systems inherit from `System(entt::registry& reg)` and store it as `reg`

### System patterns

Two options for new systems:

**Option A** — Use the `DEFINE_SYSTEM` macro in `src/logic/systems/system_list.h`:
```cpp
DEFINE_SYSTEM(LLMDebugSys)
```
Then implement `preinit/init/tick/finish` in a `.cpp` file.

**Option B** — Write a full class (like `UISys`/`ConversationSys`), place in a relevant subdirectory (`src/ai/speech/`), with explicit constructor taking `entt::registry& reg`.

Phase 5 uses **Option B** for `SpeechBubbleSys` (lives in `src/ai/speech/`) and **Option A** for `LLMDebugSys` (lives alongside other logic systems).

---

## 1. Settings Additions

### 1.1 Add batching fields to `Settings::LLMConfig`

In `src/logic/settings/settings.h`, the `LLMConfig` struct added in Phase 4 needs two more fields for the config panel's batching controls:

```cpp
struct LLMConfig {
    // ... existing Phase 4 fields ...
    bool batching_enabled = false;
    int batch_size = 3;

    template<class Archive>
    void serialize(Archive& ar) {
        ar(/* ... existing ... */
           CEREAL_NVP(batching_enabled), CEREAL_NVP(batch_size));
    }
};
```

---

## 2. Entity State Panel (`SpeechBubbleSys`)

### 2.1 Header

Create `src/ai/speech/speech_bubble_sys.h`:

```cpp
#pragma once
#include "logic/systems/system.h"
#include <entt/entt.hpp>

namespace dy {

class SpeechBubbleSys : public System {
public:
    SpeechBubbleSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "SpeechBubble"; }
    void tick(double dt) override;
};

}
```

### 2.2 Implementation

Create `src/ai/speech/speech_bubble_sys.cpp`:

```cpp
#include "speech_bubble_sys.h"

#include <ai/aic.h>
#include <ai/memory/ai_memory.h>
#include <ai/personality/personality.h>
#include <logic/components/position.h>

#include <imgui/imgui.h>

using namespace dy;

void SpeechBubbleSys::tick(double dt) {

    OPTICK_EVENT();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Entity States")) {
        auto view = reg.view<AIC, Position>();
        for (auto entity : view) {
            auto& aic = view.get<AIC>(entity);
            auto& pos = view.get<Position>(entity);

            ImGui::BeginGroup();

            // Entity name
            if (auto* pers = reg.try_get<Personality>(entity)) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Entity #%u (%s)",
                    static_cast<uint32_t>(entity), pers->archetype.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Entity #%u",
                    static_cast<uint32_t>(entity));
            }

            ImGui::Separator();

            // Position: x/y from glm::vec2, height via getter
            ImGui::Text("Position: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.getHeight());

            // Current action
            if (aic.action) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "Action: %s",
                    aic.action->describe().c_str());
            } else {
                ImGui::TextDisabled("Action: (none)");
            }

            // LLM status
            if (aic.waiting_for_llm) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Waiting for LLM...");
            } else if (!aic.action_queue.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%zu actions queued",
                    aic.action_queue.size());
            }

            // Last memory event
            if (auto* mem = reg.try_get<AIMemory>(entity)) {
                if (!mem->events.empty()) {
                    ImGui::Text("Last: %s", mem->events.back().description.c_str());
                }
            }

            ImGui::EndGroup();
            ImGui::Spacing();
        }
    }
    ImGui::End();
}
```

### 2.3 Register

In `src/logic/game.cpp`, add after `set->pre_add<AISys>()`:

```cpp
#include "ai/speech/speech_bubble_sys.h"
// ...
set->pre_add<SpeechBubbleSys>();
```

---

## 3. LLM Configuration Panel (`LLMDebugSys`)

### 3.1 Add to `system_list.h`

In `src/logic/systems/system_list.h`, add inside the `namespace dy` block:

```cpp
DEFINE_SYSTEM(LLMDebugSys)
```

### 3.2 Implementation

Create `src/logic/systems/llm_debug.cpp`:

```cpp
#include "system_list.h"

#include "logic/settings/settings.h"
#include "llm/llm_manager.h"

#include <imgui/imgui.h>

using namespace dy;

void LLMDebugSys::preinit() {}
void LLMDebugSys::init() {}
void LLMDebugSys::finish() {}

void LLMDebugSys::tick(double dt) {

    OPTICK_EVENT();

    auto& s = reg.ctx<Settings>();

    // LLMManager is stored in Game, not the registry.
    // Access via registry context pointer set in Game::start().
    LLMManager* llm_mgr = nullptr;
    if (auto* ptr = reg.try_ctx<LLMManager*>()) {
        llm_mgr = *ptr;
    }

    ImGui::SetNextWindowPos(ImVec2(420, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("LLM Configuration")) {

        // Enable toggle
        ImGui::Checkbox("Enable LLM", &s.llm.enabled);

        ImGui::Separator();

        // Provider selector
        static const char* providers[] = { "ollama", "lm_studio", "claude", "openai", nullptr };
        static const char* provider_labels[] = { "Ollama", "LM Studio", "Claude API", "OpenAI", nullptr };

        int provider_idx = 0;
        for (int i = 0; providers[i]; ++i) {
            if (s.llm.provider == providers[i]) { provider_idx = i; break; }
        }
        ImGui::Text("Provider:");
        if (ImGui::Combo("##provider", &provider_idx, provider_labels)) {
            s.llm.provider = providers[provider_idx];
            if (llm_mgr) llm_mgr->configure(s.llm.provider, s.llm.model, s.llm.api_key);
        }

        // Model input
        ImGui::Text("Model:");
        static char model_buf[256] = "";
        if (model_buf[0] == '\0') {
            strncpy_s(model_buf, s.llm.model.c_str(), sizeof(model_buf) - 1);
        }
        ImGui::InputText("##model", model_buf, sizeof(model_buf));
        ImGui::SameLine();
        if (ImGui::Button("Apply##model")) {
            s.llm.model = model_buf;
            if (llm_mgr) llm_mgr->configure(s.llm.provider, s.llm.model, s.llm.api_key);
        }

        // API key (cloud providers only)
        if (s.llm.provider == "claude" || s.llm.provider == "openai") {
            ImGui::Text("API Key:");
            static char key_buf[256] = "";
            ImGui::InputText("##apikey", key_buf, sizeof(key_buf), ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            if (ImGui::Button("Apply##key")) {
                s.llm.api_key = key_buf;
                if (llm_mgr) llm_mgr->configure(s.llm.provider, s.llm.model, s.llm.api_key);
            }
        }

        ImGui::Separator();

        // Rate limiting
        if (ImGui::SliderFloat("Rate Limit (RPS)", &s.llm.rate_limit_rps, 0.5f, 20.0f)) {
            if (llm_mgr) llm_mgr->set_rate_limit(static_cast<int>(s.llm.rate_limit_rps));
        }

        // Batching (fields added to LLMConfig in section 1.1)
        if (ImGui::Checkbox("Enable Batching", &s.llm.batching_enabled)) {
            if (llm_mgr) llm_mgr->set_batching_enabled(s.llm.batching_enabled);
        }
        if (s.llm.batching_enabled) {
            if (ImGui::SliderInt("Batch Size", &s.llm.batch_size, 2, 8)) {
                if (llm_mgr) llm_mgr->set_batch_size(s.llm.batch_size);
            }
        }

        // Caching
        if (ImGui::Checkbox("Enable Caching", &s.llm.caching_enabled)) {
            if (llm_mgr) llm_mgr->set_cache_enabled(s.llm.caching_enabled);
        }

        ImGui::Separator();

        // Status
        if (llm_mgr) {
            bool available = llm_mgr->is_available();
            ImGui::TextColored(
                available ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                "Status: %s", available ? "Connected" : "Disconnected"
            );
        } else {
            ImGui::TextDisabled("LLM disabled");
        }

        // Save button
        if (ImGui::Button("Save Settings")) {
            s.save();
        }
    }
    ImGui::End();
}
```

### 3.3 LLMManager in registry context

`LLMDebugSys` needs to reach `LLMManager` at runtime. In `Game::start()`, after creating the manager, also register a pointer in the registry context:

```cpp
// In game.cpp, inside the `if (s.llm.enabled)` block:
reg.set<LLMManager*>(llm_manager.get());
```

This allows any system to retrieve it via `reg.try_ctx<LLMManager*>()`.

### 3.4 Register

In `src/logic/game.cpp`, add:
```cpp
#include "logic/systems/system_list.h"  // already included
// ...
set->pre_add<LLMDebugSys>();
```

---

## 4. Personality Inspector

### 4.1 Add to `system_list.h`

```cpp
DEFINE_SYSTEM(PersonalityInspectorSys)
```

### 4.2 Implementation

Create `src/logic/systems/personality_inspector.cpp`:

```cpp
#include "system_list.h"

#include <ai/aic.h>
#include <ai/personality/personality.h>
#include <ai/memory/ai_memory.h>

#include <imgui/imgui.h>

using namespace dy;

// Module-level state (static, not in the system struct)
static entt::entity s_selected = entt::null;

void PersonalityInspectorSys::preinit() {}
void PersonalityInspectorSys::init() {}
void PersonalityInspectorSys::finish() {}

void PersonalityInspectorSys::tick(double dt) {

    OPTICK_EVENT();

    ImGui::SetNextWindowPos(ImVec2(780, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Personality Inspector")) {

        // Build entity list
        auto view = reg.view<AIC, Personality>();
        std::vector<entt::entity> entities(view.begin(), view.end());

        if (entities.empty()) {
            ImGui::TextDisabled("No entities with Personality component");
        } else {
            // Combo selector
            std::string preview = (s_selected != entt::null && reg.valid(s_selected))
                ? "Entity #" + std::to_string(static_cast<uint32_t>(s_selected))
                : "Select...";

            if (ImGui::BeginCombo("##entity_select", preview.c_str())) {
                for (auto e : entities) {
                    std::string label = "Entity #" + std::to_string(static_cast<uint32_t>(e));
                    if (auto* pers = reg.try_get<Personality>(e)) {
                        label += " (" + pers->archetype + ")";
                    }
                    bool selected = (s_selected == e);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        s_selected = e;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Separator();

        // Display selected entity's personality
        if (s_selected != entt::null && reg.valid(s_selected)) {
            if (auto* pers = reg.try_get<Personality>(s_selected)) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Archetype:    %s", pers->archetype.c_str());
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Motivation:   %s", pers->motivation.c_str());
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Speech Style: %s", pers->speech_style.c_str());
                ImGui::Text("Seed: %u", pers->personality_seed);

                ImGui::Separator();
                ImGui::Text("Traits:");
                for (const auto& trait : pers->traits) {
                    ImGui::Text("  %s (%.2f)", trait.name.c_str(), trait.value);
                    ImGui::ProgressBar(trait.value, ImVec2(-1.0f, 0.0f));
                }
            }

            ImGui::Separator();

            // Recent memory events
            if (auto* mem = reg.try_get<AIMemory>(s_selected)) {
                ImGui::Text("Recent Events (%zu):", mem->events.size());
                int count = 0;
                for (auto it = mem->events.rbegin(); it != mem->events.rend() && count < 5; ++it, ++count) {
                    ImGui::TextDisabled("  [%s] %s", it->event_type.c_str(), it->description.c_str());
                }
            }
        }
    }
    ImGui::End();
}
```

### 4.3 Register

In `src/logic/game.cpp`:
```cpp
set->pre_add<PersonalityInspectorSys>();
```

---

## 5. Speech Bubble Creation on LLM Response

When an LLM response arrives, the decision's `thought` and `dialogue` fields can be surfaced. The `SpeechBubble` component is an optional component for displaying fleeting text (not required for the panels above, but useful for in-world display).

### 5.1 SpeechBubble Component

Define in `src/ai/speech/speech_bubble.h`:

```cpp
#pragma once
#include <string>

namespace dy {

struct SpeechBubble {
    std::string thought;
    std::string dialogue;
    float lifetime = 4.0f;
    float elapsed = 0.0f;
};

}
```

### 5.2 Emit on LLM response

In `AISys::poll_llm_results()` (`src/ai/ai.cpp`), after populating `action_queue`:

```cpp
// Attach speech bubble with thought/dialogue from LLM response
if (!decision.thought.empty() || !decision.dialogue.empty()) {
    reg.emplace_or_replace<SpeechBubble>(entity, SpeechBubble{
        .thought  = decision.thought,
        .dialogue = decision.dialogue,
    });
}
```

### 5.3 Tick down in `SpeechBubbleSys`

In `SpeechBubbleSys::tick()`, before the panel rendering, decay and erase expired bubbles:

```cpp
auto bubble_view = reg.view<SpeechBubble>();
for (auto e : bubble_view) {
    auto& b = bubble_view.get<SpeechBubble>(e);
    b.elapsed += static_cast<float>(dt);
    if (b.elapsed >= b.lifetime) {
        reg.erase<SpeechBubble>(e);
    }
}
```

---

## 6. Final `game.cpp` Additions Summary

```cpp
// Additional includes
#include "ai/speech/speech_bubble_sys.h"

// In start(), after set->pre_add<AISys>():
set->pre_add<SpeechBubbleSys>();
set->pre_add<LLMDebugSys>();
set->pre_add<PersonalityInspectorSys>();

// In start(), inside if (s.llm.enabled) block:
reg.set<LLMManager*>(llm_manager.get());
```

---

## 7. Testing Checklist

- [ ] Entity state panel renders with position, action name, LLM wait status
- [ ] "Waiting for LLM..." shown while `waiting_for_llm == true`
- [ ] Action queue count updates when LLM responds
- [ ] Last memory event text shows in panel after action completes
- [ ] LLM config panel shows current provider/model from settings
- [ ] Provider dropdown reconfigures `LLMManager` immediately
- [ ] Rate limit slider applies to active `LLMManager`
- [ ] Batching and caching toggles call correct `LLMManager` methods
- [ ] Save Settings persists to `config.6.json`
- [ ] Personality inspector lists all entities with `Personality` component
- [ ] Traits render as progress bars
- [ ] Recent memory events (last 5) display correctly
- [ ] No crash when selected entity is destroyed
- [ ] No performance regression — UI rendering stays under 1ms/frame
- [ ] Speech bubbles decay and erase after `lifetime` seconds

---

## 8. Optional Enhancements (Post-Phase 5)

- **In-world speech bubble rendering**: Project entity world position to screen space, draw ImGui tooltip at that position
- **Action history**: Timeline of past 10 completed/failed actions in personality inspector
- **Conversation viewer**: List active conversations and their messages
- **Prompt preview**: Show (sanitized) prompt being sent to LLM
- **Stats overlay**: RPS, cache hit rate, avg response time from LLMManager

---

## 9. Dependencies

- **Depends on**: Phases 1–4
- **Independent of**: Phase 6 (action implementations)
- **Purely visual** — no game logic changes

**Can be skipped or deferred** if visual feedback is not critical for current development.

---

## 10. Next Phase

**Phase 6** implements the action skeletons (Mine, Hunt, Build, Sleep, Trade, Talk, Craft, Fish, Explore, Flee) and their corresponding ECS systems.

---

**Reference Sections from Global Doc:**
- Section 13: Speech Bubbles & Action Display
- Section 14: LLM Configuration UI
