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
    None = 13,
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
