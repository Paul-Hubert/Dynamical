# Phase 3: LLM Infrastructure (ai-sdk-cpp)

**Goal**: Integrate ai-sdk-cpp library and build complete LLM request/response pipeline with caching and async worker management.

**Scope**: Core LLM integration point — standalone testable without game loop.

**Estimated Scope**: ~2000 lines of new code (ai-sdk-cpp submodule ~5000 lines already included)

**Completion Criteria**: Can submit LLM prompts and receive parameterized actions from any provider (Ollama, LM Studio, Claude, OpenAI, etc.). Full async with per-frame result polling.

---

## Phase 2 Completion Context

Phase 2 (Personality & Memory) is **complete**. The following are available for Phase 3 to consume:

| Component | File | Purpose |
|-----------|------|---------|
| `Personality` | `src/ai/personality/personality.h` | Entity personality traits, archetype, speech style |
| `PersonalityGenerator` | `src/ai/personality/personality_generator.h/cpp` | Seeded RNG for reproducible personality generation |
| `AIMemory` | `src/ai/memory/ai_memory.h/cpp` | Event tracking with timestamps and LLM-seen flags |
| `Conversation` | `src/ai/conversation/conversation.h` | Two-entity conversation with message history |
| `ConversationManager` | `src/ai/conversation/conversation_manager.h/cpp` | Stored as `Game::conversation_manager` member |
| **Key Integration**: `AIC::last_failure_reason` + `AIC::last_action_description` | `src/ai/aic.h` | Action metadata captured in `AISys::tick()` before action destruction |

**Phase 1 is complete.** Phase 3 can begin immediately.

---

## Context: Why This Phase Matters

Phases 1-2 built the data structures (actions, personalities, memory). Phase 3 connects them to an actual language model.

Key design principle: **ai-sdk-cpp is the sole HTTP client library.** All LLM communication goes through it, eliminating manual HTTP boilerplate and enabling unified provider support.

This phase is **testable independently** of the game loop (Phase 4 integration).

---

## 1. ai-sdk-cpp Integration

### 1.1 Add as Git Submodule

In project root:

```bash
git submodule add https://github.com/ClickHouse/ai-sdk-cpp lib/ai-sdk-cpp
```

### 1.2 Update CMakeLists.txt

Add to main `CMakeLists.txt`:

```cmake
# Add ai-sdk-cpp subdirectory
add_subdirectory(lib/ai-sdk-cpp)

# Link to main target
target_link_libraries(dynamical PRIVATE ai-sdk-cpp)

# Include ai-sdk-cpp headers
target_include_directories(dynamical PRIVATE lib/ai-sdk-cpp/include)
```

### 1.3 Verify Build

```bash
cmake --preset default
cmake --build build
```

**ai-sdk-cpp includes its own patched nlohmann_json** (with thread-safety fixes). No additional vcpkg dependencies needed.

---

## 2. LLM Client Wrapper

### 2.1 LLMClient Header

Create `src/llm/llm_client.h`:

```cpp
#pragma once
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

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
    std::unique_ptr<class ai::Client> client;
    std::string provider;
    std::string model;
    std::string api_key;

    // Helper to construct provider-specific clients
    void build_client();
};
```

### 2.2 LLMClient Implementation

Create `src/llm/llm_client.cpp`:

```cpp
#include "llm_client.h"
#include <ai-sdk-cpp/client.hpp>

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
    // Simple health check: try to create a completion
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
```

---

## 3. LLM Manager (Async Orchestrator)

### 3.1 Manager Header

Create `src/llm/llm_manager.h`:

```cpp
#pragma once
#include "llm_client.h"
#include "../util/util.h"  // ThreadSafe<T>
#include <queue>
#include <thread>
#include <vector>
#include <functional>
#include <memory>

struct PendingRequest {
    uint64_t request_id;
    std::string prompt;
    std::string system_prompt;
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
    uint64_t submit_request(const std::string& prompt, const std::string& system_prompt);

    // Poll for results (call from game thread each frame)
    void poll_results(std::function<void(const LLMResponse&)> on_result);

    // Check if a request is done
    bool is_result_ready(uint64_t request_id) const;

    // Get result if ready
    bool try_get_result(uint64_t request_id, LLMResponse& out);

    // Rate limiting
    void set_rate_limit(int requests_per_second);

    // Optional batching (disabled by default)
    void set_batching_enabled(bool enabled) { batching_enabled = enabled; }
    void set_batch_size(int size) { batch_size = size; }

    // Caching
    void set_cache_enabled(bool enabled) { cache_enabled = enabled; }
    bool get_cached_result(const std::string& key, json& out) const;
    void cache_result(const std::string& key, const json& result);

    // Health check
    bool is_available() const { return client.is_available(); }

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
    float time_since_last_request = 0.0f;

    // Batching
    bool batching_enabled = false;
    int batch_size = 3;
    std::vector<PendingRequest> batch_accumulator;

    // Shutdown flag
    std::atomic<bool> should_shutdown = false;

    uint64_t next_request_id = 1;

    void worker_thread_main();
};
```

### 3.2 Manager Implementation

Create `src/llm/llm_manager.cpp`:

```cpp
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
    std::string cache_key = prompt.substr(0, 100);  // Simplified key
    if (cache_enabled) {
        if (auto cached = get_cached_result(cache_key, nullptr); cached) {
            // Return immediately (cache hit)
            LLMResponse cached_response;
            cached_response.success = true;
            if (get_cached_result(cache_key, cached_response.parsed_json)) {
                result_queue.lock([&](auto& q) { q[id] = cached_response; });
                return id;
            }
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
        if (batch_accumulator.size() >= batch_size) {
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
            std::string cache_key = req.prompt.substr(0, 100);
            cache_result(cache_key, response.parsed_json);
        }

        // Store result
        result_queue.lock([&](auto& q) { q[req.request_id] = response; });

        time_since_last_request = 0.0f;
    }
}
```

---

## 4. Prompt Builder

### 4.1 PromptBuilder Header

Create `src/llm/prompt_builder.h`:

```cpp
#pragma once
#include <string>
#include <entt/entt.hpp>
#include <ai/personality/personality.h>
#include <ai/memory/ai_memory.h>

struct PromptContext {
    entt::entity entity_id;
    const Personality* personality = nullptr;
    const AIMemory* memory = nullptr;
    std::string entity_name;
    float hunger = 0.0f;
    float energy = 0.0f;
    std::string nearby_entities;  // JSON array of nearby entity info
    std::string nearby_resources;  // JSON array of resources
};

// Helper: Build PromptContext from entity
inline PromptContext build_context(entt::registry& reg, entt::entity entity, const std::string& name) {
    PromptContext ctx{
        .entity_id = entity,
        .entity_name = name
    };

    // Retrieve Phase 2 components
    if (reg.all_of<Personality>(entity)) {
        ctx.personality = &reg.get<Personality>(entity);
    }
    if (reg.all_of<AIMemory>(entity)) {
        ctx.memory = &reg.get<AIMemory>(entity);
    }

    // TODO: Populate hunger, energy from BasicNeeds component in Phase 4
    // TODO: Populate nearby_entities and nearby_resources via spatial queries

    return ctx;
}

class PromptBuilder {
public:
    // Build a complete prompt for LLM decision-making
    std::string build_decision_prompt(const PromptContext& ctx);

    // Build a system prompt (instruction to behave as this personality)
    std::string build_system_prompt(const Personality* personality);

    // Build context about available actions
    std::string build_available_actions_prompt();

private:
    std::string format_personality(const Personality* p) const;
    std::string format_memory(const AIMemory* mem) const;
    std::string format_unread_messages(const AIMemory* mem) const;
};
```

### 4.2 PromptBuilder Implementation

Create `src/llm/prompt_builder.cpp`:

```cpp
#include "prompt_builder.h"
#include <ai/actions/action_registry.h>
#include <sstream>

std::string PromptBuilder::build_decision_prompt(const PromptContext& ctx) {
    std::ostringstream oss;

    oss << "You are " << ctx.entity_name << ", an AI entity in a simulation world.\n\n";

    // Personality (from Phase 2)
    if (ctx.personality) {
        oss << "=== YOUR PERSONALITY ===\n";
        // Use Personality::to_prompt_text() for consistent formatting
        oss << ctx.personality->to_prompt_text() << "\n\n";
    }

    // Current state
    oss << "=== YOUR STATE ===\n";
    oss << "Hunger: " << ctx.hunger << "/10\n";
    oss << "Energy: " << ctx.energy << "/10\n\n";

    // Memory and unread messages
    if (ctx.memory) {
        oss << "=== YOUR MEMORY ===\n";
        oss << format_memory(ctx.memory) << "\n\n";

        if (!ctx.memory->unread_messages.empty()) {
            oss << "=== UNREAD MESSAGES ===\n";
            oss << format_unread_messages(ctx.memory) << "\n\n";
        }
    }

    // Nearby entities and resources
    if (!ctx.nearby_entities.empty()) {
        oss << "=== NEARBY ENTITIES ===\n" << ctx.nearby_entities << "\n\n";
    }

    if (!ctx.nearby_resources.empty()) {
        oss << "=== NEARBY RESOURCES ===\n" << ctx.nearby_resources << "\n\n";
    }

    // Available actions
    oss << "=== AVAILABLE ACTIONS ===\n";
    oss << build_available_actions_prompt() << "\n\n";

    // Instructions
    oss << "=== YOUR TASK ===\n";
    oss << "Decide on your next actions. Return a JSON object with:\n";
    oss << "{\n";
    oss << "  \"thought\": \"brief reasoning\",\n";
    oss << "  \"actions\": [\n";
    oss << "    {\"action\": \"ActionName\", \"param1\": \"value1\", ...},\n";
    oss << "    ...\n";
    oss << "  ],\n";
    oss << "  \"dialogue\": \"optional spoken thought\"\n";
    oss << "}\n\n";

    oss << "You will execute actions in order. Choose 2-5 actions.\n";

    return oss.str();
}

std::string PromptBuilder::build_system_prompt(const Personality* personality) {
    std::ostringstream oss;

    if (!personality) {
        return "You are an AI entity in a simulation. Make decisions based on your state and personality.";
    }

    oss << "You are a " << personality->archetype << " with the following traits:\n";
    for (const auto& trait : personality->traits) {
        oss << "- " << trait.name << ": " << trait.value << "\n";
    }
    oss << "\nYour motivation is: " << personality->motivation << "\n";
    oss << "Your speech style is: " << personality->speech_style << "\n";
    oss << "\nAlways respond as JSON. Make decisions that align with your personality.";

    return oss.str();
}

std::string PromptBuilder::build_available_actions_prompt() {
    const auto& registry = ActionRegistry::instance();
    const auto& descriptors = registry.get_all();

    std::ostringstream oss;
    for (const auto* desc : descriptors) {
        oss << "- " << desc->name << ": " << desc->description << "\n";
    }
    return oss.str();
}

std::string PromptBuilder::format_personality(const Personality* p) const {
    std::ostringstream oss;
    oss << "Archetype: " << p->archetype << "\n";
    oss << "Motivation: " << p->motivation << "\n";
    oss << "Speech Style: " << p->speech_style << "\n";
    oss << "Traits:\n";
    for (const auto& trait : p->traits) {
        oss << "  - " << trait.name << " (" << trait.value << ")\n";
    }
    return oss.str();
}

std::string PromptBuilder::format_memory(const AIMemory* mem) const {
    if (mem->events.empty()) {
        return "No notable events.";
    }

    std::ostringstream oss;
    // Show recent events (prioritize unseen ones from Phase 2)
    int count = 0;
    for (int i = mem->events.size() - 1; i >= 0 && count < 5; --i) {
        const auto& evt = mem->events[i];
        // Prioritize unseen events to LLM, but include context
        std::string marker = evt.seen_by_llm ? "[cached] " : "[new] ";
        oss << "- " << marker << "[" << evt.event_type << "] " << evt.description;
        oss << " (" << evt.timestamp << ")\n";
        count++;
    }

    // Mark all shown events as seen (Phase 2 integration)
    // Note: In Phase 4, after LLM processes, call mem->mark_all_seen()
    return oss.str();
}

std::string PromptBuilder::format_unread_messages(const AIMemory* mem) const {
    std::ostringstream oss;
    for (const auto& msg : mem->unread_messages) {
        oss << "- " << msg << "\n";
    }
    return oss.str();
}
```

---

## 5. Response Parser

### 5.1 ResponseParser Header

Create `src/llm/response_parser.h`:

```cpp
#pragma once
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "../ai/actions/action_id.h"
#include "../ai/actions/action_params.h"

using json = nlohmann::json;

struct ParsedAction {
    ActionID action_id;
    ActionParams params;
    std::string thought;
};

struct LLMDecisionResult {
    std::vector<ParsedAction> actions;
    std::string thought;
    std::string dialogue;
    bool success = false;
};

class ResponseParser {
public:
    // Parse LLM JSON response into actionable decisions
    LLMDecisionResult parse(const json& response_json);

private:
    ActionID parse_action_id(const std::string& action_name);
    ActionParams parse_params(const json& params_json, ActionID action_id);
};
```

### 5.2 ResponseParser Implementation

Create `src/llm/response_parser.cpp`:

```cpp
#include "response_parser.h"
#include <iostream>

LLMDecisionResult ResponseParser::parse(const json& response_json) {
    LLMDecisionResult result;

    try {
        result.thought = response_json.value("thought", "");
        result.dialogue = response_json.value("dialogue", "");

        const auto& actions_array = response_json.at("actions");
        if (!actions_array.is_array()) {
            result.success = false;
            return result;
        }

        for (const auto& action_obj : actions_array) {
            std::string action_name = action_obj.value("action", "");
            if (action_name.empty()) continue;

            ActionID id = parse_action_id(action_name);
            if (id == ActionID::None) continue;

            ActionParams params = parse_params(action_obj, id);

            result.actions.push_back(ParsedAction{
                .action_id = id,
                .params = params,
                .thought = result.thought
            });
        }

        result.success = !result.actions.empty();
    } catch (const std::exception& e) {
        std::cerr << "Error parsing LLM response: " << e.what() << "\n";
        result.success = false;
    }

    return result;
}

ActionID ResponseParser::parse_action_id(const std::string& action_name) {
    if (action_name == "Wander") return ActionID::Wander;
    if (action_name == "Eat") return ActionID::Eat;
    if (action_name == "Harvest") return ActionID::Harvest;
    if (action_name == "Mine") return ActionID::Mine;
    if (action_name == "Hunt") return ActionID::Hunt;
    if (action_name == "Build") return ActionID::Build;
    if (action_name == "Sleep") return ActionID::Sleep;
    if (action_name == "Trade") return ActionID::Trade;
    if (action_name == "Talk") return ActionID::Talk;
    if (action_name == "Craft") return ActionID::Craft;
    if (action_name == "Fish") return ActionID::Fish;
    if (action_name == "Explore") return ActionID::Explore;
    if (action_name == "Flee") return ActionID::Flee;
    return ActionID::None;
}

ActionParams ResponseParser::parse_params(const json& params_json, ActionID action_id) {
    ActionParams params;

    // Common parameters
    params.target_type = params_json.value("target", "");
    params.target_name = params_json.value("target_name", "");
    params.message = params_json.value("message", "");
    params.resource = params_json.value("resource", "");
    params.recipe = params_json.value("recipe", "");
    params.structure = params_json.value("structure", "");
    params.direction = params_json.value("direction", "");
    params.amount = params_json.value("amount", 1);
    params.duration = params_json.value("duration", 0.0f);

    // Trade offer
    if (params_json.contains("offer")) {
        const auto& offer = params_json.at("offer");
        params.trade_offer.give_item = offer.value("give", "");
        params.trade_offer.give_amount = offer.value("give_amount", 1);
        params.trade_offer.want_item = offer.value("want", "");
        params.trade_offer.want_amount = offer.value("want_amount", 1);
    }

    return params;
}
```

---

## 6. Decision Cache

### 6.1 DecisionCache Header

Create `src/llm/decision_cache.h`:

```cpp
#pragma once
#include <string>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class DecisionCache {
public:
    // Generate a cache key from entity state + personality seed
    std::string generate_key(
        uint32_t personality_seed,
        float hunger,
        float energy,
        const std::string& nearby_summary
    ) const;

    // Get cached decision
    bool get(const std::string& key, json& out) const;

    // Store decision
    void put(const std::string& key, const json& decision);

    // Clear cache
    void clear() { cache.clear(); }

    size_t size() const { return cache.size(); }

private:
    std::map<std::string, json> cache;

    // Discretize continuous values for better cache hits
    int discretize_hunger(float hunger) const;
    int discretize_energy(float energy) const;
};
```

### 6.2 DecisionCache Implementation

Create `src/llm/decision_cache.cpp`:

```cpp
#include "decision_cache.h"
#include <sstream>
#include <iomanip>

std::string DecisionCache::generate_key(
    uint32_t personality_seed,
    float hunger,
    float energy,
    const std::string& nearby_summary
) const {
    std::ostringstream oss;
    oss << std::hex << personality_seed << "_";
    oss << discretize_hunger(hunger) << "_";
    oss << discretize_energy(energy) << "_";
    oss << std::hash<std::string>{}(nearby_summary);
    return oss.str();
}

bool DecisionCache::get(const std::string& key, json& out) const {
    auto it = cache.find(key);
    if (it != cache.end()) {
        out = it->second;
        return true;
    }
    return false;
}

void DecisionCache::put(const std::string& key, const json& decision) {
    cache[key] = decision;
}

int DecisionCache::discretize_hunger(float hunger) const {
    // 0-3=low, 4-6=med, 7-10=high
    if (hunger < 3.5f) return 0;
    if (hunger < 6.5f) return 1;
    return 2;
}

int DecisionCache::discretize_energy(float energy) const {
    if (energy < 3.5f) return 0;
    if (energy < 6.5f) return 1;
    return 2;
}
```

---

## 7. ConversationManager Access (Phase 2 Integration)

From Phase 2, `ConversationManager` is stored as a member in the `Game` class:

```cpp
// In Game class
ConversationManager conversation_manager;
```

To access from systems or Phase 3 components:
- Pass `Game&` or store a reference in the system
- Access as: `game.conversation_manager`

Example in Phase 4 integration:
```cpp
// In AISys::tick() or LLM prompt builder
auto& cm = game.conversation_manager;  // from Game reference
auto active_convs = cm.get_active_for(entity);
```

---

## 8. CMakeLists.txt Updates

Phase 2 files are **automatically included** via the existing `GLOB_RECURSE` pattern:
```cmake
file(GLOB_RECURSE sources ./src/main.cpp ./src/*.cpp ./src/*.h)
```

Add Phase 3 files explicitly:
```cmake
target_sources(dynamical PRIVATE
    src/llm/llm_client.h
    src/llm/llm_client.cpp
    src/llm/llm_manager.h
    src/llm/llm_manager.cpp
    src/llm/prompt_builder.h
    src/llm/prompt_builder.cpp
    src/llm/response_parser.h
    src/llm/response_parser.cpp
    src/llm/decision_cache.h
    src/llm/decision_cache.cpp
)
```

---

## 9. Testing Checklist

### Standalone Testing (no game integration yet)

- [ ] ai-sdk-cpp builds without errors
- [ ] LLMClient configures all providers (Ollama, LM Studio, Claude, OpenAI)
- [ ] LLMClient::request() connects to provider and returns valid response
- [ ] LLMClient::is_available() detects unavailable servers correctly
- [ ] LLMManager::submit_request() queues requests without blocking
- [ ] LLMManager worker threads process requests and populate result queue
- [ ] LLMManager::poll_results() drains results correctly
- [ ] Rate limiting prevents spam (< configured RPS)
- [ ] Batching accumulates requests correctly (when enabled)
- [ ] Decision cache stores and retrieves JSON correctly
- [ ] PromptBuilder generates valid decision prompts
- [ ] ResponseParser correctly extracts ActionID and parameters from JSON
- [ ] ResponseParser handles malformed JSON gracefully (returns empty actions)
- [ ] All threading is safe (ThreadSafe<T> wrappers used correctly)

### Integration Test (manual, before Phase 4)

```cpp
// Create a test setup
LLMManager mgr(2);
mgr.configure("ollama", "llama2");  // or any available provider

// Submit a request
uint64_t request_id = mgr.submit_request(
    "You are a hungry character. What do you do?",
    "You are a personality-driven AI entity."
);

// In a loop, poll for result
while (!mgr.is_result_ready(request_id)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

LLMResponse response;
mgr.try_get_result(request_id, response);

// Parse
ResponseParser parser;
auto decision = parser.parse(response.parsed_json);

// Verify
assert(!decision.actions.empty());
```

### Standalone Testing (no game integration yet)

- [ ] ai-sdk-cpp builds without errors
- [ ] LLMClient configures all providers (Ollama, LM Studio, Claude, OpenAI)
- [ ] LLMClient::request() connects to provider and returns valid response
- [ ] LLMClient::is_available() detects unavailable servers correctly
- [ ] LLMManager::submit_request() queues requests without blocking
- [ ] LLMManager worker threads process requests and populate result queue
- [ ] LLMManager::poll_results() drains results correctly
- [ ] Rate limiting prevents spam (< configured RPS)
- [ ] Batching accumulates requests correctly (when enabled)
- [ ] Decision cache stores and retrieves JSON correctly
- [ ] PromptBuilder::build_context() correctly retrieves Personality and AIMemory from entity (Phase 2 components)
- [ ] PromptBuilder generates valid decision prompts with personality trait information
- [ ] ResponseParser correctly extracts ActionID and parameters from JSON
- [ ] ResponseParser handles malformed JSON gracefully (returns empty actions)
- [ ] All threading is safe (ThreadSafe<T> wrappers used correctly)

### Phase 2 Integration Tests

- [ ] PromptBuilder accesses `reg.get<Personality>(entity)` without errors
- [ ] PromptBuilder accesses `reg.get<AIMemory>(entity)` without errors
- [ ] Memory events are properly formatted in prompts (event_type, description, timestamp)
- [ ] `mem->mark_all_seen()` is called after LLM processes (for Phase 4)
- [ ] ConversationManager can be accessed via `game.conversation_manager`
- [ ] LLM prompts can reference ongoing conversations

---

## 10. Error Handling

Key problems from global doc (Section 18):

- **Problem 1: LLM Response Latency** → Fully async + results polled per frame ✓
- **Problem 2: LLM Hallucination** → ResponseParser validates ActionID against enum, drops unknown actions ✓
- **Problem 6: LLM Server Unavailable** → LLMClient::is_available() + fallback flag in Phase 4
- **Problem 7: Thread Safety** → ThreadSafe<T> wrapper around all shared queues ✓

---

## 11. Dependencies

- **Depends on**:
  - Phase 1 (**COMPLETE**) — ActionID enum, ActionRegistry, PrerequisiteResolver
  - Phase 2 (**COMPLETE**) — Personality, AIMemory, ConversationManager, AIC metadata fields
- **Independent of**: Game loop (tested standalone in Phase 3)
- **Used by**: Phase 4 (integration into AISys + entity spawn)

**Phase 2 is complete. Phase 3 can begin immediately.**

### Critical Phase 2 Integration Points for Phase 3

1. **Personality Access**: `reg.get<Personality>(entity)` → archetype, speech_style, motivation, traits
2. **Memory Access**: `reg.get<AIMemory>(entity)` → events (type, description, timestamp, seen_by_llm)
3. **Memory Marking**: Call `mem->mark_all_seen()` after LLM processes decision
4. **Conversation Manager**: Access via `game.conversation_manager.find_conversation(a, b)`
5. **AIC Metadata**: Action descriptions captured in `AIC::last_action_description` for prompt enrichment

---

## 12. Next Phase

**Phase 4** (Game Integration) integrates LLMManager into the game loop:
- Modifies `AISys::tick()` to query LLM when deciding actions
- Wires LLM responses through PrerequisiteResolver
- Marks AIMemory events as seen after LLM decision
- Handles ongoing conversations via ConversationManager

Phase 4 will also implement:
- Fallback behavior when LLM unavailable
- Per-entity decision timing (not every frame)
- LLM queue prioritization for multiple entities

---

**Reference Sections from Global Doc:**
- Section 4: LLM Client Library (ai-sdk-cpp)
- Section 3.1 & 3.2: Architecture Overview & Decision Flow
- Section 11: Prompt Construction
- Section 17: Performance
- Problem/Solution sections: #1, #2, #6, #7

**Phase 2 Completion Status**: ✅ Complete (Personality, Memory, Conversation systems)
**Phase 3 Ready to Begin**: ✅ Yes (all Phase 1-2 dependencies satisfied)
