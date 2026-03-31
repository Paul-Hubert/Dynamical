#pragma once

#include "conversation.h"
#include <vector>
#include <memory>
#include <entt/entt.hpp>

namespace dy {

class ConversationManager {
public:
    ConversationManager(entt::registry& reg) {};
    ConversationManager() = default;
    ConversationManager(const ConversationManager&) = delete;
    ConversationManager(ConversationManager&&) = default;
    ConversationManager& operator=(ConversationManager&&) = default;

    // Start a new conversation between two entities
    Conversation* start_conversation(entt::entity a, entt::entity b);

    // Find active conversation between two entities
    Conversation* find_conversation(entt::entity a, entt::entity b);

    // Add a message to a conversation (sender_name defaults to "Entity#id" if empty)
    void add_message(Conversation* conv, entt::entity sender, const std::string& text,
                     const std::string& sender_name = "");

    // Conclude a conversation
    void conclude_conversation(Conversation* conv);

    // Get all active conversations for an entity
    std::vector<Conversation*> get_active_for(entt::entity entity);

    // Tick: clean up conversations with dead entities
    void tick(entt::registry& reg, float dt);

    // Get all conversations (for serialization)
    const std::vector<std::unique_ptr<Conversation>>& get_all() const { return conversations; }

private:
    std::vector<std::unique_ptr<Conversation>> conversations;
    static constexpr float CONVERSATION_TIMEOUT = 300.0f;  // 5 minutes in-game
};

} // namespace dy
