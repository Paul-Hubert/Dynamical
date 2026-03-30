#pragma once

#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "../ai/action_id.h"

using json = nlohmann::json;
using namespace dy;

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
