#ifndef CHUNK_H
#define CHUNK_H

#include <entt/entt.hpp>

#include "tile.h"

class Chunk {
public:
    constexpr static int size = 32;
    
    Tile tiles[size][size];
    const Tile& get(glm::ivec2 pos) const {
        return tiles[pos.x][pos.y];
    }
    
    Tile& get(glm::ivec2 pos) {
        return tiles[pos.x][pos.y];
    }
    
    std::vector<entt::entity> objects;
    
};

#endif
