#include "llm_manager.h"
#include <chrono>
#include <iostream>

LLMManager::LLMManager(size_t num_workers_) : num_workers(num_workers_) {
    // Start worker threads
    for (size_t i = 0; i < num_workers; ++i) {
        workers.emplace_back(&LLMManager::worker_thread_main, this);
    }
}

LLMManager::~LLMManager() {
    shutdown();
}

void LLMManager::configure(const std::string& provider, const std::string& model, const std::string& api_key) {
    client.configure(provider, model, api_key);
}

uint64_t LLMManager::submit_request(const std::string& prompt, const std::string& system_prompt) {
    uint64_t id = next_request_id++;

    // Check cache first
    std::string cache_key = prompt.substr(0, std::min(size_t(100), prompt.length()));
    if (cache_enabled) {
        json cached_result;
        if (get_cached_result(cache_key, cached_result)) {
            // Return immediately (cache hit)
            LLMResponse cached_response;
            cached_response.success = true;
            cached_response.parsed_json = cached_result;
            result_queue.lock([&](auto& q) { q[id] = cached_response; });
            return id;
        }
    }

    PendingRequest req{
        .request_id = id,
        .prompt = prompt,
        .system_prompt = system_prompt,
        .callback = nullptr
    };

    if (batching_enabled) {
        batch_accumulator.push_back(req);
        if (batch_accumulator.size() >= static_cast<size_t>(batch_size)) {
            // Flush batch
            for (const auto& br : batch_accumulator) {
                request_queue.lock([&](auto& q) { q.push(br); });
            }
            batch_accumulator.clear();
        }
    } else {
        request_queue.lock([&](auto& q) { q.push(req); });
    }

    return id;
}

void LLMManager::poll_results(std::function<void(const LLMResponse&)> on_result) {
    result_queue.lock([&](auto& q) {
        for (auto it = q.begin(); it != q.end(); ) {
            on_result(it->second);
            it = q.erase(it);
        }
    });
}

bool LLMManager::is_result_ready(uint64_t request_id) const {
    bool ready = false;
    result_queue.lock([&](const auto& q) { ready = q.count(request_id) > 0; });
    return ready;
}

bool LLMManager::try_get_result(uint64_t request_id, LLMResponse& out) {
    bool found = false;
    result_queue.lock([&](auto& q) {
        auto it = q.find(request_id);
        if (it != q.end()) {
            out = it->second;
            q.erase(it);
            found = true;
        }
    });
    return found;
}

void LLMManager::set_rate_limit(int requests_per_second) {
    rate_limit_rps = requests_per_second;
}

bool LLMManager::get_cached_result(const std::string& key, json& out) const {
    bool found = false;
    cache.lock([&](const auto& c) {
        auto it = c.find(key);
        if (it != c.end()) {
            out = it->second;
            found = true;
        }
    });
    return found;
}

void LLMManager::cache_result(const std::string& key, const json& result) {
    if (cache_enabled) {
        cache.lock([&](auto& c) { c[key] = result; });
    }
}

void LLMManager::shutdown() {
    should_shutdown = true;
    for (auto& thread : workers) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void LLMManager::worker_thread_main() {
    while (!should_shutdown) {
        PendingRequest req;
        bool got_request = false;

        request_queue.lock([&](auto& q) {
            if (!q.empty()) {
                req = q.front();
                q.pop();
                got_request = true;
            }
        });

        if (!got_request) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Apply rate limiting
        if (time_since_last_request < (1.0f / rate_limit_rps)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            // Requeue the request
            request_queue.lock([&](auto& q) { q.push(req); });
            continue;
        }

        // Make the request
        LLMRequest llm_req{.prompt = req.prompt, .system_prompt = req.system_prompt};
        LLMResponse response = client.request(llm_req);

        // Cache result if successful
        if (response.success && cache_enabled) {
            std::string cache_key = req.prompt.substr(0, std::min(size_t(100), req.prompt.length()));
            cache_result(cache_key, response.parsed_json);
        }

        // Store result
        result_queue.lock([&](auto& q) { q[req.request_id] = response; });

        time_since_last_request = 0.0f;
    }
}
