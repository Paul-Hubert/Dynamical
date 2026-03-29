#pragma once

#include "personality.h"
#include <random>

namespace dy {

class PersonalityGenerator {
public:
    // Generate a random personality
    Personality generate_random();

    // Generate from seed (for reproducibility)
    Personality generate_from_seed(uint32_t seed);

private:
    std::mt19937 rng;

    std::string pick_archetype();
    std::string pick_speech_style();
    std::string pick_motivation();
    PersonalityTrait generate_trait();
};

} // namespace dy
