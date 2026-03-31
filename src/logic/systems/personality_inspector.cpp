#include "system_list.h"

#include <ai/aic.h>
#include <ai/identity/entity_identity.h>
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

        auto view = reg.view<AIC, EntityIdentity>();
        std::vector<entt::entity> entities(view.begin(), view.end());

        if (entities.empty()) {
            ImGui::TextDisabled("No entities with identity");
        } else {
            std::string preview = (s_selected != entt::null && reg.valid(s_selected))
                ? "Entity #" + std::to_string(static_cast<uint32_t>(s_selected))
                : "Select...";
            if (s_selected != entt::null && reg.valid(s_selected)) {
                if (auto* eid = reg.try_get<EntityIdentity>(s_selected)) {
                    preview = eid->ready
                        ? eid->name + " (" + eid->personality_type + ")"
                        : "[pending] Entity#" + std::to_string(static_cast<uint32_t>(s_selected));
                }
            }

            if (ImGui::BeginCombo("##entity_select", preview.c_str())) {
                for (auto e : entities) {
                    std::string label = "Entity #" + std::to_string(static_cast<uint32_t>(e));
                    if (auto* eid = reg.try_get<EntityIdentity>(e)) {
                        if (eid->ready)
                            label = eid->name + " (" + eid->personality_type + ")";
                        else
                            label = "[pending] " + label;
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
            if (auto* eid = reg.try_get<EntityIdentity>(s_selected)) {
                if (eid->ready) {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Name: %s", eid->name.c_str());
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Type: %s", eid->personality_type.c_str());
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", eid->personality_description.c_str());
                } else {
                    ImGui::TextDisabled("Identity: waiting for LLM generation...");
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
