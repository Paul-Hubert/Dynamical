#include "map_generator.h"

#include "FastNoise/FastNoise.h"

#include "logic/factories/factory_list.h"

#include "logic/components/position.h"
#include "logic/settings/settings.h"
#include <extra/optick/optick.h>

#include "map_manager.h"

#include "util/log.h"

#include "logic/components/water_flow.h"

#include <queue>

using namespace dy;

MapGenerator::MapGenerator(entt::registry& reg, MapManager& map) : reg(reg), map(map) {
    
}

float MapGenerator::frand() {
    return (double) std::rand() / (RAND_MAX + 1u);
}

void MapGenerator::generateChunk(Chunk& chunk, glm::ivec2 pos) {
    //First pass, initialize levels at 0, so we can increment them later
    for(int i = 0; i<Chunk::size; i++) {
        for (int j = 0; j < Chunk::size; j++) {
            glm::vec2 position = pos * Chunk::size + glm::ivec2(i, j);
            chunk.get(glm::ivec2(i, j)).level = 0;
        }
    }

    auto& configurations = reg.ctx<Settings>().map_configurations;
    OPTICK_EVENT();
    
    auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
    auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();

    fnFractal->SetSource(fnSimplex);

    //Apply each perlin
    for(auto& conf : configurations) {
        fnFractal->SetOctaveCount(conf.octave_count);

        fnFractal->SetGain(conf.gain);
        fnFractal->SetLacunarity(conf.lacunarity);
        fnFractal->SetWeightedStrength(conf.weighted_strength);

        std::vector<float> noiseOutput(Chunk::size * Chunk::size);

        fnFractal->GenUniformGrid2D(noiseOutput.data(), pos.x * Chunk::size, pos.y * Chunk::size, Chunk::size,
                                    Chunk::size, conf.frequency, conf.seed);
        for (int i = 0; i < Chunk::size; i++) {
            for (int j = 0; j < Chunk::size; j++) {
                glm::vec2 position = pos * Chunk::size + glm::ivec2(i, j);

                Tile &tile = chunk.get(glm::ivec2(i, j));

                float noise = noiseOutput[j * Chunk::size + i];
                double level = 0;

                double lower_x = -1;
                double lower_y = conf.points_y.at(0);
                for (int i = 1; i < conf.points_x.size(); ++i) {
                    double x = conf.points_x.at(i);

                    double y = conf.points_y.at(i);
                    if (noise <= x) {
                        double mid_point = (noise - lower_x) / (x - lower_x);
                        level = (lower_y + (mid_point * (y - lower_y))) * conf.amplitude;
                        break;
                    } else {
                        lower_x = x;
                        lower_y = conf.points_y.at(i);
                    }
                }

                tile.level += (double) level;
            }
        }
    }


    for(int i = 0; i<Chunk::size; i++) {
        for (int j = 0; j < Chunk::size; j++) {
            glm::vec2 position = pos * Chunk::size + glm::ivec2(i, j);
            Tile &tile = chunk.get(glm::ivec2(i, j));
            setTileType(tile, position);
        }
    }

    for(int i = 0; i<Chunk::size; i++) {
        for (int j = 0; j < Chunk::size; j++) {
            glm::vec2 position = pos * Chunk::size + glm::ivec2(i, j);
            auto& tile = chunk.get(glm::ivec2(i, j));

            if (tile.terrain == Tile::grass) {
                auto plant_pos = position + glm::vec2(frand(), frand());
                if (frand() < 0.01) {
                    tile.object = dy::buildTree(reg, plant_pos);
                } else if (frand() < 0.001) {
                    tile.object = dy::buildBerryBush(reg, plant_pos);
                }
            }

            if (tile.level > 10 && frand() < 0.00007) {
                auto entity = reg.create();
                reg.emplace<WaterFlow>(entity, reg, position);
            }
        }
    }
}

void MapGenerator::setTileType(Tile& tile, glm::ivec2 pos) {
    //We then initialize the kind of tile (based on height and local slope)

    tile.terrain = Tile::nothing;

    if(tile.level < 0) tile.terrain = Tile::water;
    else if(tile.level >= 0 && tile.level < 1) tile.terrain = Tile::sand;
    else {
        tile.terrain = Tile::grass;

        auto neighbours = {
                glm::ivec2 (-1,0),
                glm::ivec2 (+1,0),
                glm::ivec2 (0,+1),
                glm::ivec2 (0,-1),
        };

        float derivative_sum = 0;
        int neighbours_count = 0;
        for(auto v : neighbours) {
            Tile* other_tile = map.getTile(pos + v);
            if(!other_tile) continue;
            derivative_sum += std::abs(other_tile->level - tile.level);
            ++neighbours_count;
        }

        if(neighbours_count > 0 && derivative_sum/neighbours_count > 1) {
            tile.terrain = Tile::stone;
        }
    }

}