#include "conversation_manager.h"
#include <chrono>
#include <sstream>
#include <iomanip>

namespace dy {

static std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

Conversation* ConversationManager::start_conversation(entt::entity a, entt::entity b) {
    auto conv = std::make_unique<Conversation>();
    conv->participant_a = a;
    conv->participant_b = b;
    conv->state = ConversationState::Active;
    conv->started_at = get_timestamp();

    auto* ptr = conv.get();
    conversations.push_back(std::move(conv));
    return ptr;
}

Conversation* ConversationManager::find_conversation(entt::entity a, entt::entity b) {
    for (auto& conv : conversations) {
        if (conv->is_between(a, b) && conv->is_active()) {
            return conv.get();
        }
    }
    return nullptr;
}

void ConversationManager::add_message(Conversation* conv, entt::entity sender,
                                      const std::string& text,
                                      const std::string& sender_name) {
    if (!conv) return;

    ConversationMessage msg;
    msg.sender = sender;
    msg.text = text;
    msg.timestamp = get_timestamp();
    msg.read = false;
    msg.sender_name = sender_name.empty()
        ? "Entity#" + std::to_string(static_cast<uint32_t>(sender))
        : sender_name;

    conv->messages.push_back(msg);
    conv->last_message_at = get_timestamp();
}

void ConversationManager::conclude_conversation(Conversation* conv) {
    if (conv) {
        conv->state = ConversationState::Concluded;
    }
}

std::vector<Conversation*> ConversationManager::get_active_for(entt::entity entity) {
    std::vector<Conversation*> result;
    for (auto& conv : conversations) {
        if ((conv->participant_a == entity || conv->participant_b == entity) && conv->is_active()) {
            result.push_back(conv.get());
        }
    }
    return result;
}

void ConversationManager::tick(entt::registry& reg, float dt) {
    // Remove conversations with dead entities
    conversations.erase(
        std::remove_if(conversations.begin(), conversations.end(), [&](const auto& conv) {
            if (!reg.valid(conv->participant_a) || !reg.valid(conv->participant_b)) {
                return true;  // Remove
            }
            return false;
        }),
        conversations.end()
    );

    // Note: Concluded conversations are kept in history for serialization/replay
}

bool Conversation::is_between(entt::entity a, entt::entity b) const {
    return (participant_a == a && participant_b == b) || (participant_a == b && participant_b == a);
}

const ConversationMessage* Conversation::get_last_unread_for(entt::entity entity) const {
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (!it->read && it->sender != entity) {
            return &(*it);
        }
    }
    return nullptr;
}

} // namespace dy
