#ifndef CHUNK_H
#define CHUNK_H

#include <entt/entt.hpp>

#include <unordered_set>

#include "tile.h"

namespace dy {
    
class Chunk {
public:
    constexpr static int size = 64;
    
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
    
    void setUpdated() {
        updated = true;
    }
    
    bool isUpdated() {
        return updated;
    }
    
private:
    Tile tiles[size][size];
    std::unordered_set<entt::entity> objects;
    
    bool updated = false;
    
};

}

#endif
