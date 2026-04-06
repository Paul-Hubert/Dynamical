#ifndef AIC_H
#define AIC_H

#include <queue>
#include <ai/action_id.h>
#include <ai/actions/action.h>

namespace dy {

class AIC {
public:
    std::unique_ptr<Action> action;
    float score = 0;
    ActionParams current_params;  // For future use in Phase 2+

    std::string last_failure_reason;     // Captured from action->failure_reason before destruction
    std::string last_action_description; // Captured from action->describe() before destruction

    // LLM-driven action queue
    std::queue<std::unique_ptr<Action>> action_queue;

    // LLM state flags
    uint64_t pending_llm_request_id = 0;  // 0 = no pending request
    bool waiting_for_llm = false;
    bool draining_llm_queue = false;  // true while consuming actions from an LLM response
};

}

#endif
