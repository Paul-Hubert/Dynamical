# Phase 5: UI & Visualization

**Goal**: Add ImGui overlays for entity state visualization, LLM decision monitoring, and runtime configuration panels.

**Scope**: Non-critical gameplay feature, but essential for observability and debugging.

**Estimated Scope**: ~1200 lines of new code

**Completion Criteria**: Speech bubbles show entity thoughts, dialogue, and current action. LLM config panel allows provider/model switching. Personality editor visible (read-only in Phase 5).

---

## Context: Why This Phase Matters

Without UI feedback, LLM decisions are invisible. Phase 5 adds:
1. **Speech bubbles** showing entity state, decision, and dialogue
2. **LLM config panel** for runtime provider switching and stats
3. **Personality inspector** for debugging unique entity traits

This phase is **purely visual** — no game logic changes. Can be implemented last if needed.

---

## 1. Speech Bubble System

### 1.1 SpeechBubbleSys Header

Create `src/ai/speech/speech_bubble_sys.h`:

```cpp
#pragma once
#include "../../logic/systems/system.h"
#include <entt/entt.hpp>

struct SpeechBubble {
    std::string thought;        // What the entity is thinking
    std::string dialogue;       // What the entity said
    std::string current_action; // Current action description
    float lifetime = 3.0f;      // Seconds to display
    float elapsed = 0.0f;
};

class SpeechBubbleSys : public System {
public:
    void tick(float dt) override;

private:
    void render_bubbles(float dt);
    void render_bubble_for(entt::entity entity, const SpeechBubble& bubble);
};
```

### 1.2 SpeechBubbleSys Implementation

Create `src/ai/speech/speech_bubble_sys.cpp`:

```cpp
#include "speech_bubble_sys.h"
#include "ai_memory.h"
#include "../aic.h"
#include "../actions/action.h"
#include "../personality/personality.h"
#include "../../logic/components/position.h"
#include "../../logic/components/object.h"
#include <imgui.h>
#include <glm/glm.hpp>

void SpeechBubbleSys::tick(float dt) {
    auto& reg = get_registry();

    // Update speech bubble lifetime
    auto view = reg.view<SpeechBubble>();
    for (auto entity : view) {
        auto& bubble = view.get<SpeechBubble>(entity);
        bubble.elapsed += dt;
        if (bubble.elapsed >= bubble.lifetime) {
            reg.erase<SpeechBubble>(entity);
        }
    }

    // Render all active bubbles
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Entity States")) {
        auto ai_view = reg.view<AIC, Position>();
        for (auto entity : ai_view) {
            auto& aic = ai_view.get<AIC>(entity);
            auto& pos = ai_view.get<Position>(entity);

            // Create or update speech bubble
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

            // State info
            ImGui::Text("Position: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);

            // Current action
            if (aic.action) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "Action: %s",
                    aic.action->describe().c_str());
            }

            // LLM status
            if (aic.waiting_for_llm) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "⏳ Waiting for LLM...");
            } else if (aic.action_queue.size() > 0) {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "✓ %lu actions queued",
                    aic.action_queue.size());
            }

            // Memory/thought
            if (auto* mem = reg.try_get<AIMemory>(entity)) {
                if (!mem->events.empty()) {
                    const auto& last_event = mem->events.back();
                    ImGui::Text("Last event: %s", last_event.description.c_str());
                }
            }

            ImGui::EndGroup();
            ImGui::Spacing();
        }
    }
    ImGui::End();
}
```

---

## 2. LLM Configuration Panel

### 2.1 LLM Config UI

Add to a new file `src/ui/llm_config_panel.h`:

```cpp
#pragma once
#include "../llm/llm_manager.h"
#include "../logic/settings.h"

class LLMConfigPanel {
public:
    void render(Settings& settings, LLMManager* llm_mgr);

private:
    static const char* provider_names[];
    static const char* model_names_for_provider(const std::string& provider);
    int current_provider_idx = 0;
    int current_model_idx = 0;
};
```

### 2.2 LLM Config Implementation

Create `src/ui/llm_config_panel.cpp`:

```cpp
#include "llm_config_panel.h"
#include <imgui.h>

const char* LLMConfigPanel::provider_names[] = {
    "Ollama", "LM Studio", "Claude API", "OpenAI", nullptr
};

const char* LLMConfigPanel::model_names_for_provider(const std::string& provider) {
    if (provider == "ollama") {
        return "llama2\0mistral\0neural-chat\0";
    } else if (provider == "lm_studio") {
        return "thebloke/mistral\0gpt4all-j\0";
    } else if (provider == "claude") {
        return "claude-3-haiku\0claude-3-sonnet\0claude-3-opus\0";
    } else if (provider == "openai") {
        return "gpt-3.5-turbo\0gpt-4\0gpt-4-turbo\0";
    }
    return "";
}

void LLMConfigPanel::render(Settings& settings, LLMManager* llm_mgr) {
    ImGui::SetNextWindowPos(ImVec2(420, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("LLM Configuration")) {
        // Enable/Disable toggle
        bool llm_enabled = settings.llm.enabled;
        if (ImGui::Checkbox("Enable LLM", &llm_enabled)) {
            settings.llm.enabled = llm_enabled;
        }

        ImGui::Separator();

        // Provider selector
        ImGui::Text("Provider:");
        int provider_idx = 0;
        if (settings.llm.provider == "ollama") provider_idx = 0;
        else if (settings.llm.provider == "lm_studio") provider_idx = 1;
        else if (settings.llm.provider == "claude") provider_idx = 2;
        else if (settings.llm.provider == "openai") provider_idx = 3;

        if (ImGui::Combo("##provider", &provider_idx, provider_names)) {
            switch (provider_idx) {
                case 0: settings.llm.provider = "ollama"; break;
                case 1: settings.llm.provider = "lm_studio"; break;
                case 2: settings.llm.provider = "claude"; break;
                case 3: settings.llm.provider = "openai"; break;
            }
            if (llm_mgr) {
                llm_mgr->configure(settings.llm.provider, settings.llm.model, settings.llm.api_key);
            }
        }

        // Model selector
        ImGui::Text("Model:");
        static char model_input[256] = "";
        ImGui::InputText("##model", model_input, sizeof(model_input));
        if (ImGui::Button("Set Model")) {
            settings.llm.model = model_input;
            if (llm_mgr) {
                llm_mgr->configure(settings.llm.provider, settings.llm.model, settings.llm.api_key);
            }
        }

        // API Key (if needed)
        if (settings.llm.provider == "claude" || settings.llm.provider == "openai") {
            ImGui::Text("API Key:");
            static char api_key_input[256] = "";
            ImGui::InputText("##api_key", api_key_input, sizeof(api_key_input), ImGuiInputTextFlags_Password);
            if (ImGui::Button("Set API Key")) {
                settings.llm.api_key = api_key_input;
                if (llm_mgr) {
                    llm_mgr->configure(settings.llm.provider, settings.llm.model, settings.llm.api_key);
                }
            }
        }

        ImGui::Separator();

        // Rate limiting
        ImGui::SliderFloat("Rate Limit (RPS)", &settings.llm.rate_limit_rps, 0.5f, 20.0f);
        if (llm_mgr) {
            llm_mgr->set_rate_limit(static_cast<int>(settings.llm.rate_limit_rps));
        }

        // Batching toggle
        ImGui::Checkbox("Enable Batching", &settings.llm.batching_enabled);
        if (settings.llm.batching_enabled) {
            ImGui::SliderInt("Batch Size", &settings.llm.batch_size, 2, 8);
        }
        if (llm_mgr) {
            llm_mgr->set_batching_enabled(settings.llm.batching_enabled);
            llm_mgr->set_batch_size(settings.llm.batch_size);
        }

        // Caching toggle
        ImGui::Checkbox("Enable Caching", &settings.llm.caching_enabled);
        if (llm_mgr) {
            llm_mgr->set_cache_enabled(settings.llm.caching_enabled);
        }

        ImGui::Separator();

        // Status
        if (llm_mgr) {
            bool available = llm_mgr->is_available();
            ImGui::TextColored(
                available ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                "Status: %s",
                available ? "Connected" : "Disconnected"
            );

            ImGui::Text("Current Provider: %s", llm_mgr->get_provider().c_str());
            ImGui::Text("Current Model: %s", llm_mgr->get_model().c_str());
        }

        ImGui::Text("Worker Threads: %d", settings.llm.worker_threads);
    }
    ImGui::End();
}
```

---

## 3. Personality Inspector

### 3.1 Personality UI

Create `src/ui/personality_inspector.h`:

```cpp
#pragma once
#include "../ai/personality/personality.h"
#include <entt/entt.hpp>

class PersonalityInspector {
public:
    void render(entt::registry& reg);

private:
    entt::entity selected_entity = entt::null;
};
```

### 3.2 Personality Inspector Implementation

Create `src/ui/personality_inspector.cpp`:

```cpp
#include "personality_inspector.h"
#include "../ai/aic.h"
#include <imgui.h>

void PersonalityInspector::render(entt::registry& reg) {
    ImGui::SetNextWindowPos(ImVec2(780, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Personality Inspector")) {
        // Entity selector
        ImGui::Text("Select Entity:");

        auto view = reg.view<AIC, Personality>();
        static int selected_idx = 0;
        std::vector<entt::entity> entities(view.begin(), view.end());

        if (!entities.empty()) {
            if (ImGui::BeginCombo("##entity_select",
                fmt::format("Entity #{}", static_cast<uint32_t>(selected_entity)).c_str())) {

                for (size_t i = 0; i < entities.size(); ++i) {
                    bool is_selected = (selected_entity == entities[i]);
                    if (ImGui::Selectable(
                        fmt::format("Entity #{}##combo", static_cast<uint32_t>(entities[i])).c_str(),
                        is_selected)) {
                        selected_entity = entities[i];
                        selected_idx = i;
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Separator();

        // Display personality details
        if (selected_entity != entt::null && reg.valid(selected_entity)) {
            if (auto* pers = reg.try_get<Personality>(selected_entity)) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Archetype: %s",
                    pers->archetype.c_str());
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Motivation: %s",
                    pers->motivation.c_str());
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Speech Style: %s",
                    pers->speech_style.c_str());

                ImGui::Separator();
                ImGui::Text("Traits:");
                for (const auto& trait : pers->traits) {
                    ImGui::Text("  %s: %.2f", trait.name.c_str(), trait.value);
                    ImGui::ProgressBar(trait.value, ImVec2(-1, 0));
                }

                ImGui::Separator();
                ImGui::Text("Seed: %u", pers->personality_seed);
            }
        }
    }
    ImGui::End();
}
```

---

## 4. Register Systems

### 4.1 Add to Game Loop

In `src/logic/game.cpp`, register UI systems:

```cpp
#include "speech/speech_bubble_sys.h"
#include "../ui/llm_config_panel.h"
#include "../ui/personality_inspector.h"

void Game::start() {
    // ... existing setup

    // Register UI systems (after all other systems)
    systems->register_system<SpeechBubbleSys>();

    // ... rest of game initialization
}

void Game::update_ui() {
    // Called from renderer after ImGui setup
    static LLMConfigPanel llm_panel;
    static PersonalityInspector pers_inspector;

    llm_panel.render(settings, llm_manager.get());
    pers_inspector.render(registry);
}
```

### 4.2 Trigger UI Update from Renderer

In `src/renderer/renderer.cpp` (or wherever ImGui is rendered):

```cpp
// After ImGui setup
game->update_ui();  // Call UI rendering
```

---

## 5. Speech Bubble Creation

When LLM decision is made or action completes:

```cpp
// In AISys::poll_llm_results() or action completion
if (auto decision = parser.parse(response.parsed_json); decision.success) {
    // Create speech bubble
    auto bubble = SpeechBubble{
        .thought = decision.thought,
        .dialogue = decision.dialogue,
        .current_action = "Planning next actions...",
        .lifetime = 5.0f
    };
    reg.emplace_or_replace<SpeechBubble>(entity, bubble);
}
```

---

## 6. CMakeLists.txt Updates

```cmake
target_sources(dynamical PRIVATE
    src/ai/speech/speech_bubble_sys.h
    src/ai/speech/speech_bubble_sys.cpp

    src/ui/llm_config_panel.h
    src/ui/llm_config_panel.cpp
    src/ui/personality_inspector.h
    src/ui/personality_inspector.cpp
)

# Ensure ImGui is linked
target_link_libraries(dynamical PRIVATE imgui)
```

---

## 7. Testing Checklist

- [ ] SpeechBubbleSys renders without crashing
- [ ] Entity state updates in real-time on UI
- [ ] LLM config panel displays current settings
- [ ] Provider/model changes apply to LLMManager
- [ ] Rate limit slider affects request rate
- [ ] Batching toggle works (visual confirmation only)
- [ ] Personality inspector lists all entities with personalities
- [ ] Selected entity personality displays correctly
- [ ] All UI elements are positioned without overlap
- [ ] No performance regression from UI rendering

---

## 8. Optional Enhancements (Post-Phase 5)

- **Thought visualization**: Show entity's reasoning as multi-line text
- **Action history**: Timeline of past 10 actions
- **Conversation viewer**: View active conversations between entities
- **Debug overlay**: Show prompt being sent to LLM (sanitized)
- **Decision graph**: Visualize prerequisite chains before execution
- **Stats dashboard**: RPS, cache hit rate, response times

---

## 9. Dependencies

- **Depends on**: Phases 1-4 (all game logic complete)
- **Independent of**: Action implementations (Phase 6)
- **Purely visual** — no impact on game logic

**Can be skipped or deferred if visual feedback is not critical.**

---

## 10. Next Phase

**Phase 6** implements the actual action skeletons (Mine, Hunt, Build, Sleep, Trade, Talk, Craft, Fish, Explore, Flee) and their corresponding ECS systems.

---

**Reference Sections from Global Doc:**
- Section 13: Speech Bubbles & Action Display
- Section 14: LLM Configuration UI
