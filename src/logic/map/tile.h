#ifndef TILE_H
#define TILE_H

#include <entt/entt.hpp>

namespace dy {

class Tile {
public:
    
    enum Type : int {
        nothing,
        dirt,
        grass,
        stone,
        sand,
        water,
        river,
        max
    }; // MUST UPDATE IN SHADER
    
    const inline static std::unordered_map<Type, float> terrain_speed = {
        {nothing, 0.0},
        {dirt, 1.0},
        {grass, 1.0},
        {stone, 0.0},
        {sand, 0.6},
        {water, 0.0},
        {river, 0.4}
    };
    
    Type terrain = nothing;
    float level = 0;
    entt::entity object = entt::null;

    // Building occupancy
    entt::entity building = entt::null;   // building occupying this tile
    enum BuildingRole : int { no_building, wall_tile, floor_tile, door_tile } building_role = no_building;

};

}

#endif
