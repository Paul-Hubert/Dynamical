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
