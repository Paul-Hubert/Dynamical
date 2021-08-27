#ifndef MAP_MANAGER_H
#define MAP_MANAGER_H

#include "entt/entt.hpp"
#include <unordered_map>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <memory>

#include "chunk.h"

class MapManager {
public:
    MapManager(entt::registry& reg);
    Chunk* getChunk(glm::ivec2 pos) const;
    Tile* getTile(glm::ivec2 pos) const;
    Chunk* generateChunk(glm::ivec2 pos);
    void generateTile(glm::ivec2 pos, Chunk& chunk, Tile& tile);
    
    glm::ivec2 getChunkPos(glm::ivec2 pos) const {
        return floor(glm::vec2(pos) / (float) Chunk::size);
    }

    glm::ivec2 getTilePos(glm::ivec2 pos) const {
        return pos - getChunkPos(pos);
    }
    
    glm::ivec2 floor(glm::vec2 pos) const {
        return glm::floor(pos);
    }
    
    ~MapManager();
private:
    entt::registry& reg;
    std::unordered_map<glm::ivec2, std::unique_ptr<Chunk>> map;
};

#endif
