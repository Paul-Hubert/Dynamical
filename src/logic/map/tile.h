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
        shallow_water,
        max
    };
    
    const inline static std::unordered_map<Type, float> terrain_speed = {
        {nothing, 0.0},
        {dirt, 1.0},
        {grass, 1.0},
        {stone, 0.0},
        {sand, 0.6},
        {water, 0.0},
        {shallow_water, 0.4}
    };
    
    Type terrain = dirt;
    float level = 0;
    entt::entity object = entt::null;
    
};

}

#endif
