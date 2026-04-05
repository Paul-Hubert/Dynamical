#include "prompt_builder.h"
#include "../ai/action_registry.h"
#include <sstream>

std::string PromptBuilder::build_decision_prompt(const PromptContext& ctx) {
    std::ostringstream oss;

    oss << "You are " << ctx.entity_name << ", an AI entity in a simulation world.\n\n";

    // Identity
    if (!ctx.personality_type.empty()) {
        oss << "=== YOUR IDENTITY ===\n";
        oss << "Name: " << ctx.entity_name << "\n";
        oss << "Personality: " << ctx.personality_type;
        if (!ctx.personality_description.empty())
            oss << " — " << ctx.personality_description;
        oss << "\n\n";
    }

    // Current state
    oss << "=== YOUR STATE ===\n";
    oss << "Hunger: " << ctx.hunger << "/10\n\n";

    // Memory and unread messages
    if (ctx.memory) {
        oss << "=== YOUR MEMORY ===\n";
        oss << format_memory(ctx.memory) << "\n\n";

        if (!ctx.memory->unread_messages.empty()) {
            oss << "=== UNREAD MESSAGES ===\n";
            oss << format_unread_messages(ctx.memory) << "\n\n";
        }
    }

    // Active conversation — show brief note only; dialogue is managed by TalkAction
    if (!ctx.conversation_partner.empty()) {
        oss << "=== ACTIVE CONVERSATION ===\n";
        oss << "You are currently in conversation with " << ctx.conversation_partner << ".\n\n";
    }

    // Nearby people
    if (!ctx.nearby_human_names.empty()) {
        oss << "=== NEARBY PEOPLE ===\n" << ctx.nearby_human_names << "\n\n";
    }

    if (!ctx.nearby_resources.empty()) {
        oss << "=== NEARBY RESOURCES ===\n" << ctx.nearby_resources << "\n\n";
    }

    // Nearby buildings
    if (!ctx.nearby_buildings.empty()) {
        oss << "=== NEARBY BUILDINGS ===\n" << ctx.nearby_buildings << "\n\n";
    }

    // Village needs
    if (!ctx.village_needs.empty()) {
        oss << "=== VILLAGE STATUS ===\n" << ctx.village_needs << "\n\n";
    }

    // Buildable options
    if (!ctx.buildable_options.empty()) {
        oss << "=== BUILDING OPTIONS ===\n" << ctx.buildable_options << "\n\n";
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
    oss << "}\n\n";

    oss << "You will execute actions in order. Choose 1-5 actions.\n";

    return oss.str();
}

std::string PromptBuilder::build_system_prompt(const std::string& personality_type,
                                               const std::string& personality_description) {
    if (personality_type.empty()) {
        return "You are an AI entity in a simulation. Make decisions based on your state and personality.";
    }
    std::ostringstream oss;
    oss << "You are a " << personality_type << ".\n";
    if (!personality_description.empty())
        oss << personality_description << "\n";
    oss << "\nAlways respond as JSON. Make decisions that align with your personality.";
    return oss.str();
}

std::string PromptBuilder::build_available_actions_prompt() {
    std::ostringstream oss;
    for (std::size_t i = 0; i < k_action_count; ++i) {
        if (k_action_implemented[i]) {
            oss << "- " << k_action_names[i];

            // Add action-specific descriptions
            if (std::string(k_action_names[i]) == "Mine") {
                oss << ": Mine stone from nearby stone terrain. Yields stone resources. Can be repeated infinitely.";
            }
            if (std::string(k_action_names[i]) == "Build") {
                oss << ": Construct a building. Types: small_building, medium_building, large_building. "
                    << "Requires wood and stone (cost scales with size). "
                    << "Example: {\"action\": \"Build\", \"params\": {\"structure\": \"medium_building\"}}";
            }
            oss << "\n";
        }
    }
    return oss.str();
}

std::string PromptBuilder::format_memory(const AIMemory* mem) const {
    if (mem->events.empty()) {
        return "No notable events.";
    }

    std::ostringstream oss;
    int count = 0;
    for (int i = static_cast<int>(mem->events.size()) - 1; i >= 0 && count < 5; --i) {
        const auto& evt = mem->events[i];
        std::string marker = evt.seen_by_llm ? "[cached] " : "[new] ";
        oss << "- " << marker << "[" << evt.event_type << "] " << evt.description;
        oss << " (" << evt.timestamp << ")\n";
        count++;
    }
    return oss.str();
}

std::string PromptBuilder::format_unread_messages(const AIMemory* mem) const {
    std::ostringstream oss;
    for (const auto& msg : mem->unread_messages) {
        oss << "- " << msg << "\n";
    }
    return oss.str();
}

std::string PromptBuilder::build_dialogue_prompt(const std::string& personality_type,
                                                  const std::string& personality_description,
                                                  const std::string& speaker_name,
                                                  const std::string& partner_name,
                                                  const std::vector<ConversationMessage>& history) {
    std::ostringstream oss;
    oss << "You are " << speaker_name << " speaking with " << partner_name << ".\n";
    if (!personality_type.empty()) {
        oss << "You are a " << personality_type;
        if (!personality_description.empty()) oss << ": " << personality_description;
        oss << "\n";
    }
    oss << "\n";

    if (!history.empty()) {
        oss << "=== CONVERSATION SO FAR ===\n";
        for (const auto& msg : history)
            oss << msg.sender_name << ": " << msg.text << "\n";
        oss << "\n";
    }

    oss << "It is your turn to speak as " << speaker_name << ". "
        << "Reply naturally and in character. "
        << "If the conversation has reached a natural conclusion, set \"done\" to true.\n\n";

    oss << "Return JSON:\n"
        << "{\n"
        << "  \"thought\": \"your brief internal reaction (not spoken)\",\n"
        << "  \"reply\": \"what you say out loud\",\n"
        << "  \"done\": false\n"
        << "}\n";

    return oss.str();
}

std::string PromptBuilder::build_summary_prompt(const std::string& name_a,
                                                 const std::string& name_b,
                                                 const std::vector<ConversationMessage>& history) {
    std::ostringstream oss;
    oss << "Summarize the following conversation between " << name_a << " and " << name_b
        << " in 1-3 sentences. Capture key facts, agreements, or information exchanged. "
        << "Be specific about names, items, or places mentioned.\n\n";

    oss << "=== CONVERSATION ===\n";
    for (const auto& msg : history)
        oss << msg.sender_name << ": " << msg.text << "\n";

    oss << "\nReturn JSON: { \"summary\": \"...\" }\n";
    return oss.str();
}
