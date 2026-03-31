#pragma once

#include <string>
#include <vector>
#include <entt/entt.hpp>
#include <cereal/cereal.hpp>

namespace dy {

enum class ConversationState {
    Active,
    Concluded,
    Waiting
};

struct ConversationMessage {
    entt::entity sender;        // Who said this?
    std::string sender_name;    // "Entity#42" for reference
    std::string text;
    std::string timestamp;      // When was it said?
    bool read = false;          // Has the recipient read this?

    template <class Archive>
    void serialize(Archive& ar) {
        ar(sender_name, text, timestamp, read);
    }
};

struct Conversation {
    entt::entity participant_a;
    entt::entity participant_b;
    std::vector<ConversationMessage> messages;
    ConversationState state = ConversationState::Active;

    // Metadata
    std::string started_at;
    std::string last_message_at;

    bool is_active() const { return state == ConversationState::Active; }
    bool is_between(entt::entity a, entt::entity b) const;
    const ConversationMessage* get_last_unread_for(entt::entity entity) const;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(participant_a, participant_b, messages, state, started_at, last_message_at);
    }
};

} // namespace dy
