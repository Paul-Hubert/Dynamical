#include "map_generator.h"

#include "FastNoise/FastNoise.h"

#include "logic/factories/factory_list.h"

#include "logic/components/position.h"
#include "logic/components/map_configuration.h"
#include <extra/optick/optick.h>

#include "map_manager.h"

#include "util/log.h"

#include "logic/components/water_flow.h"

#include <queue>

using namespace dy;

MapGenerator::MapGenerator(entt::registry& reg, MapManager& map) : reg(reg), map(map) {
    
}

struct PriorityElement {
    PriorityElement(glm::vec2 pos, float level) : pos(pos), level(level) {}
    PriorityElement() {
        level = std::numeric_limits<float>::max();
    }
    glm::vec2 pos;
    float level;

};

inline bool operator < (const PriorityElement a, const PriorityElement b) {
    return a.level > b.level;
}

void MapGenerator::fillRiver(glm::vec2 pos, Tile* tile) {

    std::priority_queue<PriorityElement, std::vector<PriorityElement>> queue;

    queue.emplace(pos, tile->level);

    while(!queue.empty()) {

        PriorityElement min = queue.top();
        queue.pop();

        Tile* lowest_tile = map.getTile(min.pos);

        if(lowest_tile->terrain == Tile::water) {
            return;
        }

        lowest_tile->terrain = Tile::river;

        map.getChunk(map.getChunkPos(min.pos))->setUpdated();

        for(int x = -1; x<=1; x++) {
            for(int y = -1; y<=1; y++) {
                if(abs(x) + abs(y) != 1) continue;
                glm::vec2 adj = min.pos + glm::vec2(x, y);

                Tile* tile = map.getTile(adj);

                if(!tile) {
                    map.generateChunk(map.getChunkPos(adj));
                    tile = map.getTile(adj);
                }

                if(tile->terrain == Tile::river) continue;

                if(tile->level < min.level) {
                    //queue = std::priority_queue<PriorityElement, std::vector<PriorityElement>>();
                }

                queue.emplace(adj, tile->level);

            }
        }

    }

}

void MapGenerator::generateRiver(glm::vec2 pos, Tile* tile) {
    
    tile->terrain = Tile::river;
    
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

            if(tile2->terrain == Tile::river) {
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
    
    generateRiver(lowest_pos, lowest_tile);
    
}

float MapGenerator::frand() {
    return (double) std::rand() / (RAND_MAX + 1u);
}

void MapGenerator::generateChunk(Chunk& chunk, glm::ivec2 pos) {

    auto& conf = reg.ctx<MapConfiguration>();
    OPTICK_EVENT();
    
    auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
    auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();

    fnFractal->SetSource(fnSimplex);
    fnFractal->SetOctaveCount(conf.octave_count);

    fnFractal->SetGain(conf.gain);
    fnFractal->SetLacunarity(conf.lacunarity);
    fnFractal->SetWeightedStrength(conf.weighted_strength);

    std::vector<float> noiseOutput(Chunk::size * Chunk::size);

    fnFractal->GenUniformGrid2D(noiseOutput.data(), pos.x * Chunk::size, pos.y * Chunk::size, Chunk::size, Chunk::size, conf.frequency, conf.seed);
    
    for(int i = 0; i<Chunk::size; i++) {
        for(int j = 0; j<Chunk::size; j++) {
            glm::vec2 position = pos*Chunk::size + glm::ivec2(i,j);
            
            Tile& tile = chunk.get(glm::ivec2(i,j));
            
            float noise = noiseOutput[j * Chunk::size + i];
            double level;

            double lower_x = -1;
            double lower_y = conf.points_y.at(0);
            for(auto i = 1; i < conf.points_x.size(); ++i) {
                double x = conf.points_x.at(i);

                double y = conf.points_y.at(i);
                if(noise <= x) {
                    double mid_point = (noise - lower_x) / (x - lower_x);
                    level = (lower_y + (mid_point * (y-lower_y)))*conf.amplitude;
                    break;
                } else {
                    lower_x = x;
                    lower_y = conf.points_y.at(i);
                }
            }


            tile.level = (double)level;
            
            tile.terrain = Tile::nothing;
            if(level < 0) tile.terrain = Tile::water;
            else if(level >= 0 && level < 1) tile.terrain = Tile::sand;
            else if(level >= 1 && level < 10) tile.terrain = Tile::grass;
            else if(level >= 10) tile.terrain = Tile::stone;
            
            if(tile.terrain == Tile::grass) {
                auto plant_pos = position + glm::vec2(frand(), frand());
                if(frand()< 0.03) {
                    tile.object = dy::buildTree(reg, plant_pos);
                } else if(frand()< 0.001) {
                    tile.object = dy::buildBerryBush(reg, plant_pos);
                }
            }
            
        }
    }

    for(int i = 0; i<Chunk::size; i++) {
        for(int j = 0; j<Chunk::size; j++) {
            glm::vec2 position = pos*Chunk::size + glm::ivec2(i,j);

            Tile& tile = chunk.get(glm::ivec2(i,j));

            if(tile.level > 10 && frand()< 0.00007) {
                auto entity = reg.create();
                reg.emplace<WaterFlow>(entity, reg, position);
                //fillRiver(position, &tile);
            }

        }
    }


    
}
