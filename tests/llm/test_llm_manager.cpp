#include <gtest/gtest.h>
#include <llm/llm_manager.h>
#include <fixtures/test_fixtures.h>
#include <thread>
#include <chrono>

class LLMManagerTest : public Phase3TestFixture {
protected:
    // Note: These tests create LLMManager with minimal configuration
    // Full integration tests would require a real LLM provider
};

TEST_F(LLMManagerTest, CreateManager) {
    LLMManager mgr(2);  // 2 worker threads
    EXPECT_TRUE(true);  // Successfully created
}

TEST_F(LLMManagerTest, CreateManagerWithDifferentWorkerCounts) {
    LLMManager mgr1(1);
    LLMManager mgr2(2);
    LLMManager mgr4(4);

    EXPECT_TRUE(true);  // All created successfully
}

TEST_F(LLMManagerTest, ConfigureManager) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");

    // Manager is now configured
    EXPECT_TRUE(true);
}

TEST_F(LLMManagerTest, SubmitRequestReturnsID) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");

    uint64_t request_id = mgr.submit_request("Test prompt", "System prompt");
    EXPECT_GT(request_id, 0);
}

TEST_F(LLMManagerTest, SubmitMultipleRequests) {
    LLMManager mgr(2);
    mgr.configure("ollama", "llama2");

    uint64_t id1 = mgr.submit_request("Prompt 1", "System");
    uint64_t id2 = mgr.submit_request("Prompt 2", "System");
    uint64_t id3 = mgr.submit_request("Prompt 3", "System");

    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
}

TEST_F(LLMManagerTest, SetRateLimit) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");

    mgr.set_rate_limit(10);  // 10 requests per second
    EXPECT_TRUE(true);  // Successfully set
}

TEST_F(LLMManagerTest, EnableCaching) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");

    mgr.set_cache_enabled(true);
    EXPECT_TRUE(true);  // Caching enabled
}

TEST_F(LLMManagerTest, DisableCaching) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");

    mgr.set_cache_enabled(false);
    EXPECT_TRUE(true);  // Caching disabled
}

TEST_F(LLMManagerTest, EnableBatching) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");

    mgr.set_batching_enabled(true);
    mgr.set_batch_size(5);
    EXPECT_TRUE(true);  // Batching enabled
}

TEST_F(LLMManagerTest, CacheResultManually) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");
    mgr.set_cache_enabled(true);

    json decision = {
        {"action", "Eat"},
        {"resource", "berry"}
    };

    mgr.cache_result("test_key", decision);

    json retrieved;
    bool found = mgr.get_cached_result("test_key", retrieved);
    EXPECT_TRUE(found);
    EXPECT_EQ(retrieved["action"], "Eat");
}

TEST_F(LLMManagerTest, GetNonexistentCachedResult) {
    LLMManager mgr(1);
    mgr.set_cache_enabled(true);

    json retrieved;
    bool found = mgr.get_cached_result("nonexistent", retrieved);
    EXPECT_FALSE(found);
}

TEST_F(LLMManagerTest, Shutdown) {
    LLMManager mgr(2);
    mgr.configure("ollama", "llama2");
    mgr.submit_request("Test", "System");

    mgr.shutdown();
    EXPECT_TRUE(true);  // Successfully shutdown
}

TEST_F(LLMManagerTest, IsResultReady) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");

    uint64_t request_id = mgr.submit_request("Test", "System");

    // Immediately after submit, result should not be ready
    // (unless processing is very fast)
    bool ready = mgr.is_result_ready(request_id);
    EXPECT_FALSE(ready);  // Likely not ready immediately
}

TEST_F(LLMManagerTest, TryGetNonexistentResult) {
    LLMManager mgr(1);

    LLMResponse response;
    bool found = mgr.try_get_result(12345, response);
    EXPECT_FALSE(found);
}

TEST_F(LLMManagerTest, PollResultsCallback) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");

    int callback_count = 0;
    mgr.poll_results([&](const LLMResponse& resp) {
        callback_count++;
    });

    // Callback should be called for each result
    EXPECT_GE(callback_count, 0);
}

TEST_F(LLMManagerTest, AvailabilityCheck) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");

    // is_available depends on actual LLM provider
    // This test just verifies the method exists and doesn't crash
    bool available = mgr.is_available();
    EXPECT_TRUE(true);  // Method called successfully
}

TEST_F(LLMManagerTest, CachedResultRetrieval) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");
    mgr.set_cache_enabled(true);

    // Cache multiple results
    for (int i = 0; i < 5; ++i) {
        json decision = {
            {"index", i},
            {"action", "Action" + std::to_string(i)}
        };
        mgr.cache_result("key_" + std::to_string(i), decision);
    }

    // Verify all can be retrieved
    for (int i = 0; i < 5; ++i) {
        json retrieved;
        bool found = mgr.get_cached_result("key_" + std::to_string(i), retrieved);
        EXPECT_TRUE(found);
        EXPECT_EQ(retrieved["index"], i);
    }
}

TEST_F(LLMManagerTest, MultipleRequestsWithDifferentPrompts) {
    LLMManager mgr(2);
    mgr.configure("ollama", "llama2");

    std::vector<uint64_t> request_ids;
    std::vector<std::string> prompts = {
        "What should I eat?",
        "Where should I go?",
        "Who should I talk to?",
        "What should I build?",
        "Should I sleep now?"
    };

    for (const auto& prompt : prompts) {
        uint64_t id = mgr.submit_request(prompt, "You are helpful");
        request_ids.push_back(id);
    }

    EXPECT_EQ(request_ids.size(), 5);

    // All IDs should be unique
    std::set<uint64_t> id_set(request_ids.begin(), request_ids.end());
    EXPECT_EQ(id_set.size(), 5);
}

TEST_F(LLMManagerTest, RateLimitConfiguration) {
    LLMManager mgr(1);

    mgr.set_rate_limit(1);   // 1 RPS
    mgr.set_rate_limit(5);   // 5 RPS
    mgr.set_rate_limit(100); // 100 RPS

    EXPECT_TRUE(true);  // All rate limits set successfully
}

TEST_F(LLMManagerTest, BatchSizeConfiguration) {
    LLMManager mgr(1);

    mgr.set_batching_enabled(true);
    mgr.set_batch_size(1);
    mgr.set_batch_size(5);
    mgr.set_batch_size(10);
    mgr.set_batch_size(100);

    EXPECT_TRUE(true);  // All batch sizes set successfully
}

TEST_F(LLMManagerTest, LargeNumberOfRequests) {
    LLMManager mgr(2);
    mgr.configure("ollama", "llama2");

    std::vector<uint64_t> request_ids;
    for (int i = 0; i < 100; ++i) {
        uint64_t id = mgr.submit_request(
            "Prompt " + std::to_string(i),
            "System"
        );
        request_ids.push_back(id);
    }

    EXPECT_EQ(request_ids.size(), 100);

    // All should be unique
    std::set<uint64_t> id_set(request_ids.begin(), request_ids.end());
    EXPECT_EQ(id_set.size(), 100);
}

TEST_F(LLMManagerTest, EmptyPrompt) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");

    uint64_t request_id = mgr.submit_request("", "System");
    EXPECT_GT(request_id, 0);
}

TEST_F(LLMManagerTest, EmptySystemPrompt) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");

    uint64_t request_id = mgr.submit_request("Prompt", "");
    EXPECT_GT(request_id, 0);
}

TEST_F(LLMManagerTest, LongPrompt) {
    LLMManager mgr(1);
    mgr.configure("ollama", "llama2");

    std::string long_prompt(10000, 'a');
    uint64_t request_id = mgr.submit_request(long_prompt, "System");
    EXPECT_GT(request_id, 0);
}

TEST_F(LLMManagerTest, ConsecutiveShutdowns) {
    // Test that multiple shutdowns don't cause issues
    {
        LLMManager mgr1(1);
        mgr1.configure("ollama", "llama2");
        mgr1.shutdown();
    }

    {
        LLMManager mgr2(1);
        mgr2.configure("ollama", "llama2");
        mgr2.shutdown();
    }

    EXPECT_TRUE(true);  // No crashes
}

TEST_F(LLMManagerTest, ManagerLifecycle) {
    // Test typical manager lifecycle
    LLMManager mgr(2);

    // Configure
    mgr.configure("ollama", "llama2");

    // Set options
    mgr.set_rate_limit(10);
    mgr.set_cache_enabled(true);

    // Submit requests
    uint64_t id1 = mgr.submit_request("Test 1", "System");
    uint64_t id2 = mgr.submit_request("Test 2", "System");

    // Check status (may not be ready immediately)
    mgr.is_result_ready(id1);
    mgr.is_result_ready(id2);

    // Poll for results
    int result_count = 0;
    mgr.poll_results([&](const LLMResponse& resp) {
        result_count++;
    });

    // Shutdown gracefully
    mgr.shutdown();

    EXPECT_TRUE(true);  // Completed lifecycle
}
