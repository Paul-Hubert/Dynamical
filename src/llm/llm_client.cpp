#include "llm_client.h"
#include <ai/openai.h>
#include <ai/anthropic.h>
#include "util/log.h"

void LLMClient::configure(const std::string& provider_name, const std::string& model_name, const std::string& key) {
    provider = provider_name;
    model = model_name;
    api_key = key;
    build_client();
}

void LLMClient::build_client() {
    if (provider == "ollama") {
        client = ai::openai::create_client("", "http://localhost:11434/v1");
    } else if (provider == "lm_studio") {
        client = ai::openai::create_client("", "http://localhost:1234");
    } else if (provider == "claude") {
        client = ai::anthropic::create_client(api_key);
    } else if (provider == "openai") {
        client = ai::openai::create_client(api_key);
    } else {
        // Assume OpenAI-compatible custom URL
        client = ai::openai::create_client(api_key, provider);
    }
}

bool LLMClient::is_available() const {
    // Simple health check: just check if client is configured
    if (!client.has_value()) return false;
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

    if (!client.has_value()) {
        response.success = false;
        response.error_message = "LLM client not configured";
        return response;
    }

    // Determine host:port for logging
    std::string host_port;
    if (provider == "ollama") {
        host_port = "localhost:11434";
    } else if (provider == "lm_studio") {
        host_port = "localhost:1234";
    } else if (provider == "claude") {
        host_port = "api.anthropic.com:443";
    } else if (provider == "openai") {
        host_port = "api.openai.com:443";
    } else {
        host_port = provider;  // Custom URL
    }

    // Log request details
    dy::log(dy::Level::debug) << "[LLM Request] Provider: " << provider << ", Model: " << model
                              << ", Host: " << host_port << "\n";
    dy::log(dy::Level::debug) << "[LLM System Prompt] " << req.system_prompt << "\n";
    dy::log(dy::Level::debug) << "[LLM User Prompt] " << req.prompt << "\n";

    try {
        ai::GenerateOptions opts(model, req.system_prompt, req.prompt);
        opts.max_tokens = req.max_tokens;
        opts.temperature = static_cast<double>(req.temperature);

        auto result = client->generate_text(opts);

        if (result) {
            response.raw_text = result.text;
            response.success = true;

            // Try to parse as JSON
            try {
                response.parsed_json = json::parse(response.raw_text);
            } catch (...) {
                // Not JSON, keep raw_text only
            }

            // Log response
            dy::log(dy::Level::debug) << "[LLM Response] " << response.raw_text << "\n";
        } else {
            response.success = false;
            response.error_message = result.error_message();
            dy::log(dy::Level::debug) << "[LLM Error] " << response.error_message << "\n";
        }
    } catch (const std::exception& e) {
        response.success = false;
        response.error_message = std::string(e.what());
        dy::log(dy::Level::debug) << "[LLM Exception] " << response.error_message << "\n";
    }

    return response;
}
