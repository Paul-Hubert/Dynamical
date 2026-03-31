#pragma once

#include "llm_client.h"
#include "../util/util.h"  // ThreadSafe<T>
#include <queue>
#include <thread>
#include <vector>
#include <functional>
#include <memory>
#include <map>
#include <optional>

using namespace dy;

struct PendingRequest {
    uint64_t request_id;
    std::string prompt;
    std::string system_prompt;
    float temperature = 0.7f;
    std::optional<int> seed;
    std::function<void(uint64_t, const LLMResponse&)> callback;  // Game thread callback
};

struct CachedResult {
    json decision_json;
    std::string cache_key;
};

class LLMManager {
public:
    LLMManager(size_t num_workers = 2);
    ~LLMManager();

    // Configure LLM client
    void configure(const std::string& provider, const std::string& model, const std::string& api_key = "");

    // Submit async request (non-blocking)
    uint64_t submit_request(const std::string& prompt, const std::string& system_prompt,
                            float temperature = 0.7f, std::optional<int> seed = std::nullopt);

    // Poll for results (call from game thread each frame)
    void poll_results(std::function<void(const LLMResponse&)> on_result);

    // Check if a request is done
    bool is_result_ready(uint64_t request_id);

    // Get result if ready
    bool try_get_result(uint64_t request_id, LLMResponse& out);

    // Rate limiting
    void set_rate_limit(int requests_per_second);

    // Optional batching (disabled by default)
    void set_batching_enabled(bool enabled) { batching_enabled = enabled; }
    void set_batch_size(int size) { batch_size = size; }

    // Caching
    void set_cache_enabled(bool enabled) { cache_enabled = enabled; }
    bool get_cached_result(const std::string& key, json& out);
    void cache_result(const std::string& key, const json& result);

    // Health check
    bool is_available() const { return client.is_available(); }
    int get_workers_alive() const { return workers_alive; }

    // Shutdown (called at game exit)
    void shutdown();

private:
    LLMClient client;
    std::vector<std::thread> workers;
    size_t num_workers;

    // Thread-safe queues
    ThreadSafe<std::queue<PendingRequest>> request_queue;
    ThreadSafe<std::map<uint64_t, LLMResponse>> result_queue;
    ThreadSafe<std::map<std::string, json>> cache;

    // Rate limiting
    int rate_limit_rps = 5;  // Requests per second
    std::chrono::steady_clock::time_point last_request_time;

    // Batching
    bool batching_enabled = false;
    int batch_size = 3;
    std::vector<PendingRequest> batch_accumulator;

    // Caching
    bool cache_enabled = false;

    // Shutdown flag
    std::atomic<bool> should_shutdown = false;

    // Worker health tracking
    std::atomic<int> workers_alive = 0;

    uint64_t next_request_id = 1;

    void worker_thread_main();
};
