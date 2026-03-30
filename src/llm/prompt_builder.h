#pragma once

#include <string>
#include <entt/entt.hpp>
#include "../ai/personality/personality.h"
#include "../ai/memory/ai_memory.h"
#include "../logic/components/basic_needs.h"

using namespace dy;

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

    if (reg.all_of<BasicNeeds>(entity)) {
        ctx.hunger = reg.get<BasicNeeds>(entity).hunger;
    }

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
