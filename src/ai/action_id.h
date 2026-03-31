#ifndef ACTION_ID_H
#define ACTION_ID_H

#include <array>
#include <string_view>
#include <string>
#include <entt/entt.hpp>

// Single source of truth for all action types.
// X(id, name_string, implemented)
// To expose an action to the LLM: flip false → true.
#define DY_ACTIONS(X)               \
    X(Wander,  "Wander",  true)     \
    X(Eat,     "Eat",     true)     \
    X(Harvest, "Harvest", true)     \
    X(Mine,    "Mine",    false)    \
    X(Hunt,    "Hunt",    false)    \
    X(Build,   "Build",   false)    \
    X(Sleep,   "Sleep",   false)    \
    X(Trade,   "Trade",   false)    \
    X(Talk,    "Talk",    true)     \
    X(Craft,   "Craft",   false)    \
    X(Fish,    "Fish",    false)    \
    X(Explore, "Explore", false)    \
    X(Flee,    "Flee",    false)

namespace dy {

/// Enumeration of all possible action types
enum class ActionID : int {
#define X(id, str, impl) id,
    DY_ACTIONS(X)
#undef X
    None  // sentinel; value == number of real actions
};

static constexpr std::size_t k_action_count = static_cast<std::size_t>(ActionID::None);

static constexpr std::array<std::string_view, k_action_count> k_action_names = {
#define X(id, str, impl) str,
    DY_ACTIONS(X)
#undef X
};

static constexpr std::array<bool, k_action_count> k_action_implemented = {
#define X(id, str, impl) impl,
    DY_ACTIONS(X)
#undef X
};

/// Trade offer structure for Trade and Talk actions
struct TradeOffer {
    std::string give_item;          // "berry"
    int give_amount = 1;            // Amount to give
    std::string want_item;          // "wood"
    int want_amount = 1;            // Amount to want
    bool accept = false;            // For acceptance responses
};

/// Parameters for action execution
struct ActionParams {
    // Target specification
    std::string target_type;        // "oak_tree", "stone_deposit", "berry_bush", etc.
    entt::entity target_entity = entt::null;

    // Entity interaction (Talk/Trade)
    std::string target_name;        // "Entity#42"
    std::string message;

    // Resource specification
    std::string resource;           // "wood", "stone", "berry"
    int amount = 1;

    // Crafting
    std::string recipe;             // "wooden_sword", "stone_pickaxe"

    // Building
    std::string structure;          // "house", "wall", "storage"

    // Generic movement
    std::string direction;          // "north", "south", "east", "west"
    float duration = 0.0f;          // Duration in seconds (for Sleep, etc.)

    // Trade offer structure
    TradeOffer trade_offer;

    ActionParams() = default;
};

} // namespace dy

#endif
