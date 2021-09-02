#include "map_generator.h"

#include "FastNoise/FastNoise.h"

#include "logic/factories/factory_list.h"

#include "logic/components/position.h"
#include <extra/optick/optick.h>

using namespace dy;

MapGenerator::MapGenerator(entt::registry& reg) : reg(reg) {
    
}

void MapGenerator::generateChunk(Chunk& chunk, glm::ivec2 pos) {
    
    OPTICK_EVENT();
    
    auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
    auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();

    fnFractal->SetSource(fnSimplex);
    fnFractal->SetOctaveCount(10);
    
    std::vector<float> noiseOutput(Chunk::size * Chunk::size);

    fnFractal->GenUniformGrid2D(noiseOutput.data(), pos.x * Chunk::size, pos.y * Chunk::size, Chunk::size, Chunk::size, 0.003f, 1337);
    
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
                auto plant_pos = position + glm::vec2((float) std::rand() / (RAND_MAX + 1u), (float) std::rand() / (RAND_MAX + 1u));
                if((float) std::rand() / (RAND_MAX + 1u)< 0.03) {
                    auto entity = dy::buildTree(reg, plant_pos);
                    tile.object = entity;
                } else if((float) std::rand() / (RAND_MAX + 1u)< 0.001) {
                    auto entity = dy::buildBerryBush(reg, plant_pos);
                    tile.object = entity;
                }
            }
            
        }
    }
    
}
