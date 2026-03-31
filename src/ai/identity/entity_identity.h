#pragma once

#include <string>
#include <cstdint>
#include <cereal/cereal.hpp>

namespace dy {

struct EntityIdentity {
    std::string name;
    std::string personality_type;        // e.g. "trader", "hermit", "socialite"
    std::string personality_description; // one vivid sentence

    bool ready = false;
    uint64_t pending_request_id = 0;     // transient — not serialized

    template <class Archive>
    void serialize(Archive& ar) {
        ar(name, personality_type, personality_description, ready);
    }
};

} // namespace dy
