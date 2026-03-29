#pragma once

#include <string>
#include <vector>
#include <cereal/cereal.hpp>

namespace dy {

struct PersonalityTrait {
    std::string name;           // "friendliness", "industriousness", "curiosity"
    float value;                // 0.0 to 1.0
    std::string description;    // "Very friendly and outgoing"

    template <class Archive>
    void serialize(Archive& ar) {
        ar(name, value, description);
    }
};

struct Personality {
    std::string archetype;      // "explorer", "builder", "socialite", "hermit"
    std::string speech_style;   // "formal", "casual", "poetic", "blunt"
    std::string motivation;     // "wealth", "knowledge", "relationships", "comfort"

    std::vector<PersonalityTrait> traits;

    // Generated unique identifier
    uint32_t personality_seed = 0;

    // Full personality description for LLM prompts
    std::string to_prompt_text() const;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(archetype, speech_style, motivation, traits, personality_seed);
    }
};

} // namespace dy
