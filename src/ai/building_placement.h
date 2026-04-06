#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <vector>
#include "logic/components/building.h"
#include "logic/map/map_manager.h"
#include "logic/map/tile.h"
#include "util/log.h"

namespace dy {

struct PlacementResult {
    bool success;
    glm::ivec2 origin;             // bottom-left corner
    glm::ivec2 door_position;      // absolute door tile
    std::vector<glm::ivec2> tiles;  // all occupied tiles
};

inline PlacementResult find_building_site(
    entt::registry& reg,
    MapManager& map,
    glm::vec2 entity_pos,
    const BuildingTemplate& tmpl
) {
    PlacementResult result{false, {0, 0}, {0, 0}, {}};

    dy::log() << "[Build/Placement] Searching for '" << tmpl.name
        << "' (" << tmpl.footprint.x << "x" << tmpl.footprint.y << ")"
        << " near (" << entity_pos.x << "," << entity_pos.y << ")\n";

    glm::ivec2 entity_tile = map.floor(entity_pos);
    int max_radius = 30;

    int rejected_null = 0, rejected_terrain = 0, rejected_occupied = 0, rejected_steep = 0;

    // Find nearest existing building for clustering bonus
    float nearest_building_dist = std::numeric_limits<float>::max();
    for (auto [entity, building, pos] : reg.view<Building, Position>().each()) {
        float dist = glm::distance(entity_pos, glm::vec2(pos.x, pos.y));
        nearest_building_dist = std::min(nearest_building_dist, dist);
    }

    int best_adjacency = -1;
    glm::ivec2 best_origin = {0, 0};
    float best_distance_to_entity = std::numeric_limits<float>::max();

    // Spiral search outward from entity position
    for (int radius = 0; radius <= max_radius; radius++) {
        for (int dx = -radius; dx <= radius; dx++) {
            for (int dy = -radius; dy <= radius; dy++) {
                // Only check tiles on the perimeter of the current radius
                if (std::abs(dx) != radius && std::abs(dy) != radius) continue;

                glm::ivec2 candidate_origin = entity_tile + glm::ivec2(dx, dy);

                // Check if all footprint tiles are valid
                bool all_valid = true;
                int adjacency_score = 0;
                float height_variance = 0.0f;
                float avg_height = 0.0f;
                int tile_count = 0;

                std::vector<glm::ivec2> occupied_tiles;
                for (int x = 0; x < tmpl.footprint.x; x++) {
                    for (int y = 0; y < tmpl.footprint.y; y++) {
                        glm::ivec2 tile_pos = candidate_origin + glm::ivec2(x, y);
                        glm::vec2 world_pos(tile_pos);

                        // Check if chunk exists or can be generated
                        Tile* tile = map.getTile(world_pos);
                        if (tile == nullptr) {
                            all_valid = false;
                            rejected_null++;
                            break;
                        }

                        // Check terrain: must be walkable (grass, dirt, sand) and not water/stone
                        auto speed_it = Tile::terrain_speed.find(tile->terrain);
                        if (speed_it == Tile::terrain_speed.end() || speed_it->second <= 0.0f ||
                            tile->terrain == Tile::water || tile->terrain == Tile::stone) {
                            all_valid = false;
                            rejected_terrain++;
                            break;
                        }

                        // Check not already occupied by building
                        if (tile->building != entt::null || tile->building_role != Tile::no_building) {
                            all_valid = false;
                            rejected_occupied++;
                            break;
                        }

                        occupied_tiles.push_back(tile_pos);
                        avg_height += tile->level;
                        tile_count++;
                    }
                    if (!all_valid) break;
                }

                if (!all_valid || tile_count == 0) continue;

                avg_height /= tile_count;

                // Check terrain height variance (reject if too steep)
                for (const auto& tile_pos : occupied_tiles) {
                    Tile* tile = map.getTile(glm::vec2(tile_pos));
                    if (tile) {
                        height_variance = std::max(height_variance, std::abs(tile->level - avg_height));
                    }
                }

                if (height_variance > 1.0f) { rejected_steep++; continue; } // Reject steep terrain

                // Count adjacent buildings for clustering bonus
                for (int bx = -1; bx <= tmpl.footprint.x; bx++) {
                    for (int by = -1; by <= tmpl.footprint.y; by++) {
                        if (bx >= 0 && bx < tmpl.footprint.x && by >= 0 && by < tmpl.footprint.y) continue;

                        glm::ivec2 check_pos = candidate_origin + glm::ivec2(bx, by);
                        Tile* tile = map.getTile(glm::vec2(check_pos));
                        if (tile && tile->building != entt::null) {
                            adjacency_score++;
                        }
                    }
                }

                // Choose best site: prefer high adjacency, break ties by proximity to entity
                glm::vec2 candidate_center = glm::vec2(candidate_origin) + glm::vec2(tmpl.footprint) * 0.5f;
                float dist_to_entity = glm::distance(entity_pos, candidate_center);

                if (adjacency_score > best_adjacency ||
                    (adjacency_score == best_adjacency && dist_to_entity < best_distance_to_entity)) {
                    best_adjacency = adjacency_score;
                    best_origin = candidate_origin;
                    best_distance_to_entity = dist_to_entity;
                }
            }
        }

        // If we found a site in this ring, return it
        if (best_adjacency >= 0) {
            result.success = true;
            result.origin = best_origin;

            // Compute door position: center of south (min-y) edge
            glm::ivec2 door_offset = tmpl.door_offset();
            result.door_position = best_origin + door_offset;

            // Collect all occupied tiles
            for (int x = 0; x < tmpl.footprint.x; x++) {
                for (int y = 0; y < tmpl.footprint.y; y++) {
                    result.tiles.push_back(best_origin + glm::ivec2(x, y));
                }
            }

            dy::log() << "[Build/Placement] Rejected candidates:"
                << " null=" << rejected_null << " terrain=" << rejected_terrain
                << " occupied=" << rejected_occupied << " steep=" << rejected_steep << "\n";
            dy::log() << "[Build/Placement] FOUND origin("
                << result.origin.x << "," << result.origin.y << ")"
                << " adjacency=" << best_adjacency << "\n";

            return result;
        }
    }

    dy::log() << "[Build/Placement] NO SITE FOUND. Rejected candidates:"
        << " null=" << rejected_null << " terrain=" << rejected_terrain
        << " occupied=" << rejected_occupied << " steep=" << rejected_steep << "\n";
    return result;  // No valid site found
}

}  // namespace dy
