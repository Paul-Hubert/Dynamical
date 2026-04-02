#pragma once

#include <string>
#include <sstream>
#include <algorithm>
#include <entt/entt.hpp>
#include "../ai/identity/entity_identity.h"
#include "../ai/memory/ai_memory.h"
#include "../ai/conversation/conversation_manager.h"
#include "../logic/components/basic_needs.h"
#include "../logic/components/position.h"
#include "../logic/components/object.h"
#include <glm/glm.hpp>

using namespace dy;

struct PromptContext {
    entt::entity entity_id;
    std::string entity_name;
    std::string personality_type;
    std::string personality_description;
    const AIMemory* memory = nullptr;
    float hunger = 0.0f;
    float energy = 0.0f;
    std::string nearby_human_names;   // comma-separated names of nearby humans
    std::string nearby_resources;
    std::string conversation_history;
    std::string conversation_partner; // name of active conversation partner
};

// Helper: Build PromptContext from entity (identity must be ready)
inline PromptContext build_context(entt::registry& reg, entt::entity entity) {
    PromptContext ctx{ .entity_id = entity };

    // EntityIdentity — name and personality
    if (reg.all_of<EntityIdentity>(entity)) {
        const auto& id = reg.get<EntityIdentity>(entity);
        ctx.entity_name             = id.name;
        ctx.personality_type        = id.personality_type;
        ctx.personality_description = id.personality_description;
    } else {
        ctx.entity_name = "Entity#" + std::to_string(static_cast<uint32_t>(entity));
    }

    // AIMemory
    if (reg.all_of<AIMemory>(entity)) {
        ctx.memory = &reg.get<AIMemory>(entity);
    }

    // BasicNeeds
    if (reg.all_of<BasicNeeds>(entity)) {
        ctx.hunger = reg.get<BasicNeeds>(entity).hunger;
    }

    // Nearby humans (within 20 tiles)
    if (reg.all_of<Position>(entity)) {
        const auto& my_pos = reg.get<Position>(entity);
        std::ostringstream nearby_oss;
        bool first = true;
        for (auto [e, obj, pos] : reg.view<Object, Position>().each()) {
            if (e == entity || obj.id != Object::human) continue;
            float dist = glm::distance(glm::vec2(my_pos.x, my_pos.y),
                                       glm::vec2(pos.x, pos.y));
            if (dist > 20.0f) continue;
            std::string other_name = "Entity#" + std::to_string(static_cast<uint32_t>(e));
            if (reg.all_of<EntityIdentity>(e)) {
                const auto& eid = reg.get<EntityIdentity>(e);
                if (eid.ready) other_name = eid.name;
            }
            if (!first) nearby_oss << ", ";
            nearby_oss << other_name;
            first = false;
        }
        ctx.nearby_human_names = nearby_oss.str();
    }

    // Active conversation history + partner name
    if (auto* conv_mgr = reg.try_ctx<ConversationManager>()) {
        auto active = conv_mgr->get_active_for(entity);
        if (!active.empty()) {
            const auto* conv = active[0];

            // Partner name
            entt::entity partner = (conv->participant_a == entity)
                                   ? conv->participant_b : conv->participant_a;
            if (reg.valid(partner) && reg.all_of<EntityIdentity>(partner)) {
                const auto& pid = reg.get<EntityIdentity>(partner);
                if (pid.ready) ctx.conversation_partner = pid.name;
            }
            if (ctx.conversation_partner.empty())
                ctx.conversation_partner = "Entity#" + std::to_string(static_cast<uint32_t>(partner));

            // Last 6 messages
            const auto& msgs = conv->messages;
            int start = std::max(0, (int)msgs.size() - 6);
            std::ostringstream oss;
            for (int i = start; i < (int)msgs.size(); ++i)
                oss << msgs[i].sender_name << ": " << msgs[i].text << "\n";
            ctx.conversation_history = oss.str();
        }
    }

    return ctx;
}

class PromptBuilder {
public:
    std::string build_decision_prompt(const PromptContext& ctx);
    std::string build_system_prompt(const std::string& personality_type,
                                    const std::string& personality_description);
    std::string build_available_actions_prompt();

    // Dedicated dialogue prompt: asks the speaker for their next line.
    // Includes the full conversation history so the LLM never repeats itself.
    // Response JSON: { "thought": "...", "reply": "...", "done": false }
    std::string build_dialogue_prompt(const std::string& personality_type,
                                      const std::string& personality_description,
                                      const std::string& speaker_name,
                                      const std::string& partner_name,
                                      const std::vector<ConversationMessage>& history);

    // Summary prompt: asks a neutral narrator to condense the conversation.
    // Response JSON: { "summary": "..." }
    std::string build_summary_prompt(const std::string& name_a,
                                     const std::string& name_b,
                                     const std::vector<ConversationMessage>& history);

private:
    std::string format_memory(const AIMemory* mem) const;
    std::string format_unread_messages(const AIMemory* mem) const;
};
