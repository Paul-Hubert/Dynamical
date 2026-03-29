#include "decision_cache.h"
#include <sstream>
#include <iomanip>
#include <functional>

std::string DecisionCache::generate_key(
    uint32_t personality_seed,
    float hunger,
    float energy,
    const std::string& nearby_summary
) const {
    std::ostringstream oss;
    oss << std::hex << personality_seed << "_";
    oss << discretize_hunger(hunger) << "_";
    oss << discretize_energy(energy) << "_";
    oss << std::hash<std::string>{}(nearby_summary);
    return oss.str();
}

bool DecisionCache::get(const std::string& key, json& out) const {
    auto it = cache.find(key);
    if (it != cache.end()) {
        out = it->second;
        return true;
    }
    return false;
}

void DecisionCache::put(const std::string& key, const json& decision) {
    cache[key] = decision;
}

int DecisionCache::discretize_hunger(float hunger) const {
    // 0-3=low, 4-6=med, 7-10=high
    if (hunger < 3.5f) return 0;
    if (hunger < 6.5f) return 1;
    return 2;
}

int DecisionCache::discretize_energy(float energy) const {
    if (energy < 3.5f) return 0;
    if (energy < 6.5f) return 1;
    return 2;
}
