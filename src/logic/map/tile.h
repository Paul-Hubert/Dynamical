#ifndef TILE_H
#define TILE_H

#include <entt/entt.hpp>

class Tile {
public:
    constexpr static float size = 0.5f;
    entt::entity entity = entt::null;
    
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
    
    Type terrain = dirt;
    Type over = nothing;
    
    
};

#endif
