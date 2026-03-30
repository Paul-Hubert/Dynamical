#include "speech_bubble_sys.h"

#include <ai/aic.h>
#include <ai/memory/ai_memory.h>
#include <ai/personality/personality.h>
#include <ai/speech/speech_bubble.h>
#include <logic/components/position.h>

#include <imgui/imgui.h>

using namespace dy;

void SpeechBubbleSys::tick(double dt) {

    OPTICK_EVENT();

    // Decay and erase expired speech bubbles
    auto bubble_view = reg.view<SpeechBubble>();
    for (auto e : bubble_view) {
        auto& b = bubble_view.get<SpeechBubble>(e);
        b.elapsed += static_cast<float>(dt);
        if (b.elapsed >= b.lifetime) {
            reg.erase<SpeechBubble>(e);
        }
    }

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Entity States")) {
        auto view = reg.view<AIC, Position>();
        for (auto entity : view) {
            auto& aic = view.get<AIC>(entity);
            auto& pos = view.get<Position>(entity);

            ImGui::BeginGroup();

            if (auto* pers = reg.try_get<Personality>(entity)) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Entity #%u (%s)",
                    static_cast<uint32_t>(entity), pers->archetype.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Entity #%u",
                    static_cast<uint32_t>(entity));
            }

            ImGui::Separator();

            ImGui::Text("Position: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.getHeight());

            if (aic.action) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "Action: %s",
                    aic.action->describe().c_str());
            } else {
                ImGui::TextDisabled("Action: (none)");
            }

            if (aic.waiting_for_llm) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Waiting for LLM...");
            } else if (!aic.action_queue.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%zu actions queued",
                    aic.action_queue.size());
            }

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
