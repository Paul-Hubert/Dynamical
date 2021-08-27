#include "map_manager.h"

#include "util/log.h"

#include "FastNoise/FastNoise.h"

MapManager::MapManager(entt::registry& reg) : reg(reg) {
    
}

Chunk* MapManager::getChunk(glm::ivec2 pos) const {
    try {
        return map.at(pos).get();
    } catch(std::out_of_range&) {
        return nullptr;
    }
}

Chunk* MapManager::generateChunk(glm::ivec2 pos) {
    if(getChunk(pos) != nullptr) {
        dy::log(dy::warning) << "double chunk generation\n";
        return getChunk(pos);
    }
    map[pos] = std::make_unique<Chunk>();
    
    auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
    auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();

    fnFractal->SetSource( fnSimplex );
    fnFractal->SetOctaveCount( 5 );
    
    std::vector<float> noiseOutput(Chunk::size * Chunk::size);

    fnFractal->GenUniformGrid2D(noiseOutput.data(), pos.x * Chunk::size, pos.y * Chunk::size, Chunk::size, Chunk::size, 0.02f, 1337);
    
    Chunk* chunk = map[pos].get();
    for(int i = 0; i<Chunk::size; i++) {
        for(int j = 0; j<Chunk::size; j++) {
            glm::ivec2 position = pos*Chunk::size + glm::ivec2(i,j);
            
            Tile& tile = chunk->get(glm::ivec2(i,j));
            
            float noise = noiseOutput[j * Chunk::size + i];
            
            float level = noise * 20 + 5;
            
            if(level < 0) tile.terrain = Tile::water;
            else if(level > 0 && level < 3) tile.terrain = Tile::sand;
            else if(level > 3 && level < 15) tile.terrain = Tile::grass;
            else if(level > 15) tile.terrain = Tile::stone;
            
        }
    }
    return chunk;
}

void MapManager::generateTile(glm::ivec2 pos, Chunk& chunk, Tile& tile) {
    

    
}

Tile* MapManager::getTile(glm::ivec2 pos) const {
    
    Chunk* chunk = getChunk(getChunkPos(pos));
    if(chunk == nullptr) return nullptr;
    return &chunk->get(getTilePos(pos));
}


MapManager::~MapManager() {
    
}

