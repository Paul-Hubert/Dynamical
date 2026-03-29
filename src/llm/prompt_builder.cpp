#include "prompt_builder.h"
#include "../ai/action_registry.h"
#include <sstream>

std::string PromptBuilder::build_decision_prompt(const PromptContext& ctx) {
    std::ostringstream oss;

    oss << "You are " << ctx.entity_name << ", an AI entity in a simulation world.\n\n";

    // Personality (from Phase 2)
    if (ctx.personality) {
        oss << "=== YOUR PERSONALITY ===\n";
        oss << format_personality(ctx.personality) << "\n\n";
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
    for (int i = static_cast<int>(mem->events.size()) - 1; i >= 0 && count < 5; --i) {
        const auto& evt = mem->events[i];
        // Prioritize unseen events to LLM, but include context
        std::string marker = evt.seen_by_llm ? "[cached] " : "[new] ";
        oss << "- " << marker << "[" << evt.event_type << "] " << evt.description;
        oss << " (" << evt.timestamp << ")\n";
        count++;
    }

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
