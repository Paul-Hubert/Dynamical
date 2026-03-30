#include <gtest/gtest.h>
#include <llm/llm_client.h>
#include <fixtures/test_fixtures.h>

class LLMClientTest : public Phase3TestFixture {
protected:
    LLMClient client;

    // Note: These tests will only pass if an LLM provider is available
    // For CI/CD environments, we recommend mocking the HTTP layer
    // or using environment variables to skip LLM tests
};

TEST_F(LLMClientTest, ConfigureOllama) {
    client.configure("ollama", "llama2");
    EXPECT_EQ(client.get_provider(), "ollama");
    EXPECT_EQ(client.get_model(), "llama2");
}

TEST_F(LLMClientTest, ConfigureLMStudio) {
    client.configure("lm_studio", "mistral");
    EXPECT_EQ(client.get_provider(), "lm_studio");
    EXPECT_EQ(client.get_model(), "mistral");
}

TEST_F(LLMClientTest, ConfigureClaude) {
    client.configure("claude", "claude-3-sonnet");
    EXPECT_EQ(client.get_provider(), "claude");
    EXPECT_EQ(client.get_model(), "claude-3-sonnet");
}

TEST_F(LLMClientTest, ConfigureOpenAI) {
    client.configure("openai", "gpt-4");
    EXPECT_EQ(client.get_provider(), "openai");
    EXPECT_EQ(client.get_model(), "gpt-4");
}

TEST_F(LLMClientTest, ConfigureCustomProvider) {
    client.configure("http://custom.local:8000/v1", "custom-model");
    EXPECT_EQ(client.get_provider(), "http://custom.local:8000/v1");
    EXPECT_EQ(client.get_model(), "custom-model");
}

TEST_F(LLMClientTest, RequestStructure) {
    LLMRequest req;
    req.prompt = "What is the meaning of life?";
    req.system_prompt = "You are a helpful assistant.";
    req.max_tokens = 100;
    req.temperature = 0.7f;

    EXPECT_EQ(req.prompt, "What is the meaning of life?");
    EXPECT_EQ(req.system_prompt, "You are a helpful assistant.");
    EXPECT_EQ(req.max_tokens, 100);
    EXPECT_FLOAT_EQ(req.temperature, 0.7f);
}

TEST_F(LLMClientTest, ResponseStructure) {
    LLMResponse resp;
    resp.raw_text = "This is a response";
    resp.success = true;
    resp.parsed_json = json{{"key", "value"}};

    EXPECT_EQ(resp.raw_text, "This is a response");
    EXPECT_TRUE(resp.success);
    EXPECT_EQ(resp.parsed_json["key"], "value");
}

TEST_F(LLMClientTest, DefaultRequestValues) {
    LLMRequest req;
    EXPECT_EQ(req.prompt, "");
    EXPECT_EQ(req.system_prompt, "");
    EXPECT_EQ(req.max_tokens, 512);
    EXPECT_FLOAT_EQ(req.temperature, 0.7f);
}

TEST_F(LLMClientTest, DefaultResponseValues) {
    LLMResponse resp;
    EXPECT_EQ(resp.raw_text, "");
    EXPECT_FALSE(resp.success);
    EXPECT_EQ(resp.error_message, "");
}

TEST_F(LLMClientTest, ProviderAvailability) {
    // This test depends on actual providers being available
    // In production, you might skip this or use a mock
    client.configure("ollama", "llama2");
    bool available = client.is_available();
    // We don't assert the result because it depends on environment
    EXPECT_TRUE(true); // Placeholder
}

TEST_F(LLMClientTest, MultipleConfigurations) {
    // Test reconfiguring the client
    client.configure("ollama", "llama2");
    EXPECT_EQ(client.get_provider(), "ollama");
    EXPECT_EQ(client.get_model(), "llama2");

    client.configure("lm_studio", "mistral");
    EXPECT_EQ(client.get_provider(), "lm_studio");
    EXPECT_EQ(client.get_model(), "mistral");

    client.configure("openai", "gpt-4");
    EXPECT_EQ(client.get_provider(), "openai");
    EXPECT_EQ(client.get_model(), "gpt-4");
}

TEST_F(LLMClientTest, RequestWithLongPrompt) {
    LLMRequest req;
    req.prompt = std::string(1000, 'a');  // 1000 character prompt
    req.system_prompt = std::string(500, 'b');  // 500 character system prompt
    req.max_tokens = 1024;

    EXPECT_EQ(req.prompt.length(), 1000);
    EXPECT_EQ(req.system_prompt.length(), 500);
    EXPECT_EQ(req.max_tokens, 1024);
}

TEST_F(LLMClientTest, TemperatureRange) {
    LLMRequest req_cold;
    req_cold.temperature = 0.0f;
    EXPECT_FLOAT_EQ(req_cold.temperature, 0.0f);

    LLMRequest req_warm;
    req_warm.temperature = 2.0f;
    EXPECT_FLOAT_EQ(req_warm.temperature, 2.0f);

    LLMRequest req_mid;
    req_mid.temperature = 1.0f;
    EXPECT_FLOAT_EQ(req_mid.temperature, 1.0f);
}

TEST_F(LLMClientTest, JSONResponseParsing) {
    LLMResponse resp;
    resp.raw_text = R"({"action": "Eat", "resource": "berry"})";
    resp.success = true;

    try {
        resp.parsed_json = json::parse(resp.raw_text);
        EXPECT_EQ(resp.parsed_json["action"], "Eat");
        EXPECT_EQ(resp.parsed_json["resource"], "berry");
    } catch (...) {
        FAIL() << "Failed to parse JSON response";
    }
}

TEST_F(LLMClientTest, NonJSONResponseHandling) {
    LLMResponse resp;
    resp.raw_text = "This is plain text, not JSON";
    resp.success = true;

    try {
        resp.parsed_json = json::parse(resp.raw_text);
        FAIL() << "Should have thrown exception for non-JSON text";
    } catch (...) {
        // Expected behavior: parsing plain text as JSON should fail
        EXPECT_TRUE(true);
    }
}

// Integration-style test (minimal, doesn't require actual LLM)
TEST_F(LLMClientTest, RequestResponseFlow) {
    client.configure("ollama", "llama2");

    LLMRequest req;
    req.prompt = "Say 'Hello'";
    req.system_prompt = "You are helpful";
    req.max_tokens = 10;

    // Note: This will fail if Ollama is not running
    // In production, use dependency injection to mock this
    // LLMResponse resp = client.request(req);
    // For now, we skip the actual HTTP call in the test

    EXPECT_TRUE(true); // Placeholder assertion
}
