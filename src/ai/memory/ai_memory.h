#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cereal/cereal.hpp>

namespace dy {

struct MemoryEvent {
    std::string event_type;     // "harvested", "ate", "traded", "talked", "failed_action"
    std::string description;    // "Harvested 5 berries from bush near river"
    std::string timestamp;      // ISO 8601 or game-time
    bool seen_by_llm = false;   // Has this been included in an LLM prompt?

    template <class Archive>
    void serialize(Archive& ar) {
        ar(event_type, description, timestamp, seen_by_llm);
    }
};

struct AIMemory {
    std::vector<MemoryEvent> events;
    std::vector<std::string> unread_messages;  // Messages received but not yet processed by LLM

    // Mark all events as seen (called after including in LLM prompt)
    void mark_all_seen();

    // Add a new event
    void add_event(const std::string& type, const std::string& description);

    // Add a message (e.g., from conversation partner)
    void add_message(const std::string& message);

    // Clear unread messages
    void clear_unread();

    template <class Archive>
    void serialize(Archive& ar) {
        ar(events, unread_messages);
    }
};

} // namespace dy
