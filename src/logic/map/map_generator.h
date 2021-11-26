#ifndef MAP_GENERATOR_H
#define MAP_GENERATOR_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "chunk.h"


namespace dy {

class MapManager;
    
class MapGenerator {
public:
    MapGenerator(entt::registry& reg, MapManager& map);
    void generateChunk(Chunk& chunk, glm::ivec2 pos);
    void generateRiver(glm::vec2 pos);
private:
    entt::registry& reg;
    MapManager& map;
    
    float frand();
};

}

#endif
