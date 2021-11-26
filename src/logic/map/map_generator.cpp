#include "map_generator.h"

#include "FastNoise/FastNoise.h"

#include "logic/factories/factory_list.h"

#include "logic/components/position.h"
#include <extra/optick/optick.h>

#include "map_manager.h"

#include "util/log.h"

using namespace dy;

MapGenerator::MapGenerator(entt::registry& reg, MapManager& map) : reg(reg), map(map) {
    
}

void MapGenerator::generateRiver(glm::vec2 pos) {
    
    Tile* tile = map.getTile(pos);
    
    tile->terrain = Tile::shallow_water;
    
    map.getChunk(map.getChunkPos(pos))->setUpdated();

    Tile* lowest_tile = nullptr;
    glm::vec2 lowest_pos;
    float lowest_level = 100000;
    
    for(int x = -1; x<=1; x++) {
        for(int y = -1; y<=1; y++) {
            if(abs(x) + abs(y) != 1) continue;
            glm::vec2 pos2 = pos + glm::vec2(x, y);
            
            Tile* tile2 = map.getTile(pos2);
            
            if(!tile2) {
                map.generateChunk(map.getChunkPos(pos2));
                tile2 = map.getTile(pos2);
            }

            if(tile2->terrain == Tile::shallow_water) {
                continue;
            }
            
            if(tile2->level <= lowest_level) {
                lowest_level = tile2->level;
                lowest_pos = pos2;
                lowest_tile = tile2;
            }
            
        }
    }
    
    if(lowest_tile == nullptr || lowest_tile->terrain == Tile::water) {
        return;
    }
    
    generateRiver(lowest_pos);
    
}

float MapGenerator::frand() {
    return (float) std::rand() / (RAND_MAX + 1u);
}


void MapGenerator::generateChunk(Chunk& chunk, glm::ivec2 pos) {
    
    OPTICK_EVENT();
    
    auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
    auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();

    fnFractal->SetSource(fnSimplex);
    fnFractal->SetOctaveCount(10);
    
    std::vector<float> noiseOutput(Chunk::size * Chunk::size);

    fnFractal->GenUniformGrid2D(noiseOutput.data(), pos.x * Chunk::size, pos.y * Chunk::size, Chunk::size, Chunk::size, 0.003f, 1338);
    
    for(int i = 0; i<Chunk::size; i++) {
        for(int j = 0; j<Chunk::size; j++) {
            glm::vec2 position = pos*Chunk::size + glm::ivec2(i,j);
            
            Tile& tile = chunk.get(glm::ivec2(i,j));
            
            float noise = noiseOutput[j * Chunk::size + i];
            
            float level = noise * 20 + 5;
            
            tile.level = level;
            
            tile.terrain = Tile::nothing;
            if(level < 0) tile.terrain = Tile::water;
            else if(level > 0 && level < 3) tile.terrain = Tile::sand;
            else if(level > 3 && level < 15) tile.terrain = Tile::grass;
            else if(level > 15) tile.terrain = Tile::stone;
            
            if(tile.terrain == Tile::grass) {
                auto plant_pos = position + glm::vec2(frand(), frand());
                if(frand()< 0.03) {
                    auto entity = dy::buildTree(reg, plant_pos);
                    tile.object = entity;
                } else if(frand()< 0.001) {
                    auto entity = dy::buildBerryBush(reg, plant_pos);
                    tile.object = entity;
                }
            }
            
        }
    }

    for(int i = 0; i<Chunk::size; i++) {
        for(int j = 0; j<Chunk::size; j++) {
            glm::vec2 position = pos*Chunk::size + glm::ivec2(i,j);

            Tile& tile = chunk.get(glm::ivec2(i,j));

            if(tile.level > 15 && frand()< 0.0003) {
                generateRiver(position);
            }

        }
    }


    
}
