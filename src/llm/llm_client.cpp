#include "llm_client.h"
#include <ai/client.hpp>

void LLMClient::configure(const std::string& provider_name, const std::string& model_name, const std::string& key) {
    provider = provider_name;
    model = model_name;
    api_key = key;
    build_client();
}

void LLMClient::build_client() {
    // Configure based on provider
    if (provider == "ollama") {
        client = std::make_unique<ai::Client>("http://localhost:11434/v1", "", ai::Provider::OpenAI);
    } else if (provider == "lm_studio") {
        client = std::make_unique<ai::Client>("http://localhost:1234/v1", "", ai::Provider::OpenAI);
    } else if (provider == "claude") {
        client = std::make_unique<ai::Client>("https://api.anthropic.com", api_key, ai::Provider::Anthropic);
    } else if (provider == "openai") {
        client = std::make_unique<ai::Client>("https://api.openai.com/v1", api_key, ai::Provider::OpenAI);
    } else {
        // Assume OpenAI-compatible
        client = std::make_unique<ai::Client>(provider, api_key, ai::Provider::OpenAI);
    }
}

bool LLMClient::is_available() const {
    // Simple health check: just check if client is configured
    if (!client) return false;
    try {
        // A real implementation would do a lightweight health check
        // For now, assume client is available if configured
        return true;
    } catch (...) {
        return false;
    }
}

LLMResponse LLMClient::request(const LLMRequest& req) {
    LLMResponse response;

    if (!client) {
        response.success = false;
        response.error_message = "LLM client not configured";
        return response;
    }

    try {
        // Build the request using ai-sdk-cpp
        auto messages = std::vector<ai::Message>{
            ai::Message{.role = "system", .content = req.system_prompt},
            ai::Message{.role = "user", .content = req.prompt}
        };

        auto completion_options = ai::CreateCompletionOptions{
            .max_tokens = static_cast<size_t>(req.max_tokens),
            .temperature = req.temperature
        };

        auto response_obj = client->CreateCompletion(model, messages, completion_options);

        if (response_obj && !response_obj->choices.empty()) {
            response.raw_text = response_obj->choices[0].message.content;
            response.success = true;

            // Try to parse as JSON
            try {
                response.parsed_json = json::parse(response.raw_text);
            } catch (...) {
                // Not JSON, keep raw_text only
            }
        } else {
            response.success = false;
            response.error_message = "Empty response from LLM";
        }
    } catch (const std::exception& e) {
        response.success = false;
        response.error_message = std::string(e.what());
    }

    return response;
}
