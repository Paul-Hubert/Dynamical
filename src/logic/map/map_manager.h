#ifndef MAP_MANAGER_H
#define MAP_MANAGER_H

#include "entt/entt.hpp"
#include <unordered_map>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <memory>

#include "chunk.h"

#include "map_generator.h"

class MapManager {
public:
    MapManager(entt::registry& reg);
    MapManager(const MapManager&) = delete;
    Chunk* getChunk(glm::ivec2 pos) const;
    Tile* getTile(glm::vec2 pos) const;
    Chunk* generateChunk(glm::ivec2 pos);
    
    std::vector<glm::vec2> pathfind(glm::vec2 start, std::function<bool(glm::vec2)> predicate) const;
    
    void insert(entt::entity entity, glm::vec2 position);
    void move(entt::entity entity, glm::vec2 position);
    void remove(entt::entity entity);
    
    glm::ivec2 getChunkPos(glm::vec2 pos) const {
        return floor(pos / (float) Chunk::size);
    }

    glm::ivec2 getTilePos(glm::vec2 pos) const {
        return floor(pos) - getChunkPos(pos) * Chunk::size;
    }
    
    glm::ivec2 floor(glm::vec2 pos) const {
        return glm::floor(pos);
    }
    
    glm::vec2 getMousePosition() const;
    
    ~MapManager();
private:
    entt::registry& reg;
    std::unordered_map<glm::vec2, std::unique_ptr<Chunk>> map;
    MapGenerator generator;
};

#endif
