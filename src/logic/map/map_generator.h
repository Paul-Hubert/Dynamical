#ifndef MAP_GENERATOR_H
#define MAP_GENERATOR_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "chunk.h"

namespace dy {
    
class MapGenerator {
public:
    MapGenerator(entt::registry& reg);
    void generateChunk(Chunk& chunk, glm::ivec2 pos);
private:
    entt::registry& reg;
};

}

#endif
