#pragma once

#include <string>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <vector>
#include <ai/openai.h>
#include <ai/anthropic.h>

using json = nlohmann::json;

struct LLMRequest {
    std::string prompt;
    std::string system_prompt;
    int max_tokens = 512;
    float temperature = 0.7f;
};

struct LLMResponse {
    std::string raw_text;
    json parsed_json;
    bool success = false;
    std::string error_message;
};

class LLMClient {
public:
    LLMClient() = default;

    // Configure active provider and model at startup
    void configure(
        const std::string& provider,    // "ollama", "lm_studio", "claude", "openai"
        const std::string& model,       // "llama2", "claude-3-sonnet", etc.
        const std::string& api_key = ""
    );

    // Single synchronous request (for standalone testing)
    LLMResponse request(const LLMRequest& req);

    // Check if provider is available
    bool is_available() const;

    // Get current provider/model
    std::string get_provider() const { return provider; }
    std::string get_model() const { return model; }

private:
    std::optional<ai::Client> client;
    std::string provider;
    std::string model;
    std::string api_key;

    // Helper to construct provider-specific clients
    void build_client();
};
