#include "system_list.h"

#include <ai/aic.h>
#include <ai/personality/personality.h>
#include <ai/memory/ai_memory.h>

#include <imgui/imgui.h>

using namespace dy;

static entt::entity s_selected = entt::null;

void PersonalityInspectorSys::preinit() {}
void PersonalityInspectorSys::init() {}
void PersonalityInspectorSys::finish() {}

void PersonalityInspectorSys::tick(double dt) {

    OPTICK_EVENT();

    ImGui::SetNextWindowPos(ImVec2(780, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Personality Inspector")) {

        auto view = reg.view<AIC, Personality>();
        std::vector<entt::entity> entities(view.begin(), view.end());

        if (entities.empty()) {
            ImGui::TextDisabled("No entities with Personality component");
        } else {
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
