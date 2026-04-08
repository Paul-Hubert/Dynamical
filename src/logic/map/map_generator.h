#ifndef MAP_GENERATOR_H
#define MAP_GENERATOR_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "chunk.h"
#include "logic/settings/map_configuration.h"

#include <vector>

namespace dy {

class MapManager;

class MapGenerator {
public:
    MapGenerator(entt::registry& reg, MapManager& map);
    void generateChunk(Chunk& chunk, glm::ivec2 pos);
    void setTileType(Tile& tile, glm::ivec2 pos);

    // Thread-safe: generates terrain heights + basic tile types (no ECS, no neighbor lookup)
    // Can be called from worker threads. lod_level: 0=32x32, 1=16x16, 2=8x8
    static void generateTerrain(Chunk& chunk, glm::ivec2 pos, int lod_level,
                                const std::vector<MapConfiguration>& configurations);

    // Main-thread only: neighbor-based tile type refinement + entity spawning
    void finalizeChunk(Chunk& chunk, glm::ivec2 pos);

private:
    entt::registry& reg;
    MapManager& map;

    float frand();
};

}

#endif
