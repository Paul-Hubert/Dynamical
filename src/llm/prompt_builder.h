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
#include "../logic/components/building.h"
#include "../logic/components/storage.h"
#include "../logic/components/item.h"
#include "../ai/crafting/recipe_registry.h"
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
    std::string nearby_buildings;     // buildings within 30 tiles
    std::string village_needs;        // housing shortages, etc.
    std::string buildable_options;    // building types the entity can afford
    std::string inventory;            // current items in Storage
    std::string craftable_recipes;    // recipes and their ingredients
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

    // Nearby buildings (within 30 tiles)
    if (reg.all_of<Position>(entity)) {
        const auto& my_pos = reg.get<Position>(entity);
        std::ostringstream buildings_oss;
        bool first = true;
        int total_residents = 0;
        int total_capacity = 0;

        for (auto [e, building] : reg.view<Building>().each()) {
            glm::vec2 building_center(0, 0);
            if (reg.all_of<Position>(e)) {
                const auto& bpos = reg.get<Position>(e);
                building_center = glm::vec2(bpos.x, bpos.y);
            }
            float dist = glm::distance(glm::vec2(my_pos.x, my_pos.y), building_center);
            if (dist > 30.0f) continue;

            const auto& tmpl = get_building_templates()[building.type];
            if (!first) buildings_oss << ", ";
            buildings_oss << tmpl.name << " (" << tmpl.capacity << " capacity, "
                         << building.residents.size() << " residents, "
                         << (int)dist << " tiles away)";
            first = false;

            total_residents += building.residents.size();
            total_capacity += tmpl.capacity;
        }
        ctx.nearby_buildings = buildings_oss.str();

        // Compute village needs
        std::ostringstream needs_oss;
        int shortage = std::max(0, total_residents - total_capacity);
        if (shortage > 0) {
            needs_oss << "Village needs: " << shortage << " more housing (" << shortage << " entities without homes)";
        } else {
            needs_oss << "Village housing sufficient";
        }
        ctx.village_needs = needs_oss.str();
    }

    // Buildable options (based on storage)
    if (reg.all_of<Storage>(entity)) {
        const auto& storage = reg.get<Storage>(entity);
        std::ostringstream options_oss;
        bool first = true;

        for (const auto& tmpl : get_building_templates()) {
            int wood_have = storage.amount(Item::wood);
            int stone_have = storage.amount(Item::stone);
            if (wood_have >= tmpl.wood_cost() && stone_have >= tmpl.stone_cost()) {
                if (!first) options_oss << ", ";
                options_oss << tmpl.name << " (" << tmpl.wood_cost() << " wood, "
                           << tmpl.stone_cost() << " stone)";
                first = false;
            }
        }

        if (first) {
            ctx.buildable_options = "No buildings affordable with current materials";
        } else {
            ctx.buildable_options = "You can build: " + options_oss.str();
        }

        // Inventory
        std::ostringstream inv_oss;
        bool inv_first = true;
        for (int i = 0; i < static_cast<int>(Item::COUNT); ++i) {
            auto id = static_cast<Item::ID>(i);
            if (id == Item::null) continue;
            int amt = storage.amount(id);
            if (amt > 0) {
                if (!inv_first) inv_oss << ", ";
                inv_oss << Item::to_string(id) << " x" << amt;
                inv_first = false;
            }
        }
        ctx.inventory = inv_first ? "Empty" : inv_oss.str();
    }

    // Craftable recipes
    {
        const auto& recipes = RecipeRegistry::instance().all();
        std::ostringstream recipes_oss;
        for (const auto& recipe : recipes) {
            recipes_oss << "- " << recipe.name << ": ";
            bool first_input = true;
            for (auto& [item, amt] : recipe.inputs) {
                if (!first_input) recipes_oss << " + ";
                recipes_oss << Item::to_string(item) << " x" << amt;
                first_input = false;
            }
            recipes_oss << " -> " << Item::to_string(recipe.output) << " x" << recipe.output_amount << "\n";
        }
        ctx.craftable_recipes = recipes_oss.str();
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
