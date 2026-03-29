#pragma once

#include <string>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class DecisionCache {
public:
    // Generate a cache key from entity state + personality seed
    std::string generate_key(
        uint32_t personality_seed,
        float hunger,
        float energy,
        const std::string& nearby_summary
    ) const;

    // Get cached decision
    bool get(const std::string& key, json& out) const;

    // Store decision
    void put(const std::string& key, const json& decision);

    // Clear cache
    void clear() { cache.clear(); }

    size_t size() const { return cache.size(); }

private:
    std::map<std::string, json> cache;

    // Discretize continuous values for better cache hits
    int discretize_hunger(float hunger) const;
    int discretize_energy(float energy) const;
};
