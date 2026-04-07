#ifndef BUILDING_H
#define BUILDING_H

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <vector>
#include <array>
#include <string>

namespace dy {

struct BuildingTemplate {
    std::string name;                    // "small_building", "medium_building", "large_building"
    glm::ivec2 footprint;               // {2,2}, {3,3}, {4,5}
    float wall_height;                   // height of walls
    float roof_height;                   // peak above walls
    int capacity;                        // max residents

    // Dynamic cost calculation
    int area() const { return footprint.x * footprint.y; }
    int wood_cost() const { return area() * 1; }   // 1 wood per tile
    int stone_cost() const { return area() * 1; }   // 1 stone per tile

    // Door is always at center of the south (min-y) edge
    glm::ivec2 door_offset() const { return {footprint.x / 2, 0}; }
};

// Single source of truth for all building definitions
inline const std::array<BuildingTemplate, 3>& get_building_templates() {
    static const std::array<BuildingTemplate, 3> templates = {{
        {"small_building",  {2, 2}, 1.2f, 0.8f, 1},
        {"medium_building", {3, 3}, 1.5f, 1.2f, 2},
        {"large_building",  {4, 5}, 2.0f, 2.0f, 4},
    }};
    return templates;
}

struct Building {
    enum Type : int { small_building, medium_building, large_building, max };
    Type type;
    entt::entity owner;
    std::vector<entt::entity> residents;
    std::vector<glm::ivec2> occupied_tiles;   // absolute tile positions
    glm::ivec2 door_tile;                     // absolute door position
    float construction_progress = 0.0f;       // 0.0 → 1.0
    bool complete = false;
};

}

#endif
