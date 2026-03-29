#include "personality_generator.h"
#include <chrono>

namespace dy {

Personality PersonalityGenerator::generate_random() {
    uint32_t seed = static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());
    return generate_from_seed(seed);
}

Personality PersonalityGenerator::generate_from_seed(uint32_t seed) {
    rng.seed(seed);

    Personality p;
    p.personality_seed = seed;
    p.archetype = pick_archetype();
    p.speech_style = pick_speech_style();
    p.motivation = pick_motivation();

    // Generate 4-6 traits
    std::uniform_int_distribution<int> trait_count(4, 6);
    for (int i = 0; i < trait_count(rng); ++i) {
        p.traits.push_back(generate_trait());
    }

    return p;
}

std::string PersonalityGenerator::pick_archetype() {
    static const std::vector<std::string> archetypes = {
        "explorer", "builder", "socialite", "hermit", "scholar", "artist", "warrior", "merchant"
    };
    std::uniform_int_distribution<size_t> dist(0, archetypes.size() - 1);
    return archetypes[dist(rng)];
}

std::string PersonalityGenerator::pick_speech_style() {
    static const std::vector<std::string> styles = {
        "formal", "casual", "poetic", "blunt", "verbose", "minimal"
    };
    std::uniform_int_distribution<size_t> dist(0, styles.size() - 1);
    return styles[dist(rng)];
}

std::string PersonalityGenerator::pick_motivation() {
    static const std::vector<std::string> motivations = {
        "wealth", "knowledge", "relationships", "comfort", "freedom", "power", "legacy"
    };
    std::uniform_int_distribution<size_t> dist(0, motivations.size() - 1);
    return motivations[dist(rng)];
}

PersonalityTrait PersonalityGenerator::generate_trait() {
    static const std::vector<std::string> trait_names = {
        "friendliness", "industriousness", "curiosity", "bravery", "generosity",
        "ambition", "loyalty", "creativity", "patience", "humor"
    };
    std::uniform_int_distribution<size_t> name_dist(0, trait_names.size() - 1);
    std::uniform_real_distribution<float> value_dist(0.2f, 0.9f);

    std::string name = trait_names[name_dist(rng)];
    float value = value_dist(rng);

    return PersonalityTrait{
        .name = name,
        .value = value,
        .description = name + " level " + std::to_string(static_cast<int>(value * 10))
    };
}

std::string Personality::to_prompt_text() const {
    std::string result;
    result += "Archetype: " + archetype + "\n";
    result += "Speech Style: " + speech_style + "\n";
    result += "Primary Motivation: " + motivation + "\n";
    result += "Traits:\n";
    for (const auto& trait : traits) {
        result += "  - " + trait.name + ": " + std::to_string(trait.value) + "\n";
    }
    return result;
}

} // namespace dy
