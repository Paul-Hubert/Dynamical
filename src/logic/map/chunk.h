#ifndef CHUNK_H
#define CHUNK_H

#include <entt/entt.hpp>

#include <unordered_set>

#include "tile.h"

class Chunk {
public:
    constexpr static int size = 32;
    
    const Tile& get(glm::ivec2 pos) const {
        return tiles[pos.x][pos.y];
    }
    
    Tile& get(glm::ivec2 pos) {
        return tiles[pos.x][pos.y];
    }
    
    void addObject(entt::entity entity) {
        objects.insert(entity);
    }
    
    void removeObject(entt::entity entity) {
        objects.erase(entity);
    }
    
    const std::unordered_set<entt::entity>& getObjects() {
        return objects;
    }
    
private:
    Tile tiles[size][size];
    std::unordered_set<entt::entity> objects;
    
};

#endif
