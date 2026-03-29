#ifndef ACTION_ID_H
#define ACTION_ID_H

#include <string>
#include <entt/entt.hpp>

namespace dy {

/// Enumeration of all possible action types
enum class ActionID {
    Wander,
    Eat,
    Harvest,
    Mine,
    Hunt,
    Build,
    Sleep,
    Trade,
    Talk,
    Craft,
    Fish,
    Explore,
    Flee,
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

    ActionParams() = default;
};

/// Trade offer structure for Trade and Talk actions
struct TradeOffer {
    std::string give;               // "berry x5"
    std::string want;               // "wood x2"
    bool accept = false;            // For acceptance responses
};

} // namespace dy

#endif
