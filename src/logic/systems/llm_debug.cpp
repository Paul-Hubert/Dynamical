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

    LLMManager* llm_mgr = nullptr;
    if (auto* ptr = reg.try_ctx<LLMManager*>()) {
        llm_mgr = *ptr;
    }

    ImGui::SetNextWindowPos(ImVec2(420, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("LLM Configuration")) {

        ImGui::Checkbox("Enable LLM", &s.llm.enabled);

        ImGui::Separator();

        static const char* providers[]       = { "ollama", "lm_studio", "claude", "openai" };
        static const char* provider_labels[] = { "Ollama", "LM Studio", "Claude API", "OpenAI" };

        int provider_idx = 0;
        for (int i = 0; i < 4; ++i) {
            if (s.llm.provider == providers[i]) { provider_idx = i; break; }
        }
        ImGui::Text("Provider:");
        if (ImGui::Combo("##provider", &provider_idx, provider_labels, 4)) {
            s.llm.provider = providers[provider_idx];
            if (llm_mgr) llm_mgr->configure(s.llm.provider, s.llm.model, s.llm.api_key);
        }

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

        if (ImGui::SliderFloat("Rate Limit (RPS)", &s.llm.rate_limit_rps, 0.5f, 20.0f)) {
            if (llm_mgr) llm_mgr->set_rate_limit(static_cast<int>(s.llm.rate_limit_rps));
        }

        if (ImGui::Checkbox("Enable Batching", &s.llm.batching_enabled)) {
            if (llm_mgr) llm_mgr->set_batching_enabled(s.llm.batching_enabled);
        }
        if (s.llm.batching_enabled) {
            if (ImGui::SliderInt("Batch Size", &s.llm.batch_size, 2, 8)) {
                if (llm_mgr) llm_mgr->set_batch_size(s.llm.batch_size);
            }
        }

        if (ImGui::Checkbox("Enable Caching", &s.llm.caching_enabled)) {
            if (llm_mgr) llm_mgr->set_cache_enabled(s.llm.caching_enabled);
        }

        ImGui::Separator();

        if (llm_mgr) {
            bool available = llm_mgr->is_available();
            ImGui::TextColored(
                available ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                "Status: %s", available ? "Connected" : "Disconnected"
            );
        } else {
            ImGui::TextDisabled("LLM disabled");
        }

        if (ImGui::Button("Save Settings")) {
            s.save();
        }
    }
    ImGui::End();
}
