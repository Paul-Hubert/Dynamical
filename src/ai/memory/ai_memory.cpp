#include "ai_memory.h"
#include <chrono>
#include <sstream>
#include <iomanip>

namespace dy {

void AIMemory::mark_all_seen() {
    for (auto& event : events) {
        event.seen_by_llm = true;
    }
}

void AIMemory::add_event(const std::string& type, const std::string& description) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");

    events.push_back(MemoryEvent{
        .event_type = type,
        .description = description,
        .timestamp = oss.str(),
        .seen_by_llm = false
    });
}

void AIMemory::add_message(const std::string& message) {
    unread_messages.push_back(message);
}

void AIMemory::clear_unread() {
    unread_messages.clear();
}

} // namespace dy
