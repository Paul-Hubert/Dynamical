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

// Thread-safe static method: generates terrain heights + basic tile types
// No ECS access, no MapManager access — safe for worker threads
void MapGenerator::generateTerrain(Chunk& chunk, glm::ivec2 pos, int lod_level,
                                   const std::vector<MapConfiguration>& configurations) {
    OPTICK_EVENT();

    // Determine sample resolution based on LOD level
    int sample_size = Chunk::size; // 32
    if (lod_level == 1) sample_size = 16;
    else if (lod_level >= 2) sample_size = 8;
    int step = Chunk::size / sample_size;

    chunk.lod_level = lod_level;

    // First pass: initialize all tile levels to 0
    for (int i = 0; i < Chunk::size; i++) {
        for (int j = 0; j < Chunk::size; j++) {
            chunk.get(glm::ivec2(i, j)).level = 0;
        }
    }

    auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
    auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();
    fnFractal->SetSource(fnSimplex);

    // Second pass: apply FastNoise2 FBm for each configuration
    for (auto& conf : configurations) {
        fnFractal->SetOctaveCount(conf.octave_count);
        fnFractal->SetGain(conf.gain);
        fnFractal->SetLacunarity(conf.lacunarity);
        fnFractal->SetWeightedStrength(conf.weighted_strength);

        std::vector<float> noiseOutput(sample_size * sample_size);

        // Generate noise at sample resolution
        fnFractal->GenUniformGrid2D(noiseOutput.data(),
                                    pos.x * Chunk::size, pos.y * Chunk::size,
                                    sample_size, sample_size,
                                    conf.frequency * step, conf.seed);

        for (int si = 0; si < sample_size; si++) {
            for (int sj = 0; sj < sample_size; sj++) {
                float noise = noiseOutput[sj * sample_size + si];
                double level = 0;

                double lower_x = -1;
                double lower_y = conf.points_y.at(0);
                for (int k = 1; k < conf.points_x.size(); ++k) {
                    double x = conf.points_x.at(k);
                    double y = conf.points_y.at(k);
                    if (noise <= x) {
                        double mid_point = (noise - lower_x) / (x - lower_x);
                        level = (lower_y + (mid_point * (y - lower_y))) * conf.amplitude;
                        break;
                    } else {
                        lower_x = x;
                        lower_y = conf.points_y.at(k);
                    }
                }

                // Replicate to fill the step×step block
                for (int di = 0; di < step; di++) {
                    for (int dj = 0; dj < step; dj++) {
                        int ti = si * step + di;
                        int tj = sj * step + dj;
                        chunk.get(glm::ivec2(ti, tj)).level += (double)level;
                    }
                }
            }
        }
    }

    // Third pass: tile type assignment with chunk-local slope detection for stone
    for (int i = 0; i < Chunk::size; i++) {
        for (int j = 0; j < Chunk::size; j++) {
            Tile& tile = chunk.get(glm::ivec2(i, j));
            tile.terrain = Tile::nothing;
            if (tile.level < 0) {
                tile.terrain = Tile::water;
            } else if (tile.level < 1) {
                tile.terrain = Tile::sand;
            } else {
                tile.terrain = Tile::grass;

                // Slope detection using chunk-local neighbors
                constexpr glm::ivec2 offsets[] = {{-1,0}, {1,0}, {0,1}, {0,-1}};
                float derivative_sum = 0;
                int neighbours_count = 0;
                for (auto& off : offsets) {
                    int ni = i + off.x;
                    int nj = j + off.y;
                    if (ni >= 0 && ni < Chunk::size && nj >= 0 && nj < Chunk::size) {
                        derivative_sum += std::abs(chunk.get(glm::ivec2(ni, nj)).level - tile.level);
                        neighbours_count++;
                    }
                }
                if (neighbours_count > 0 && derivative_sum / neighbours_count > 1) {
                    tile.terrain = Tile::stone;
                }
            }
        }
    }
}

// Main-thread only: entity spawning (trees, bushes, water flow)
void MapGenerator::finalizeChunk(Chunk& chunk, glm::ivec2 pos) {
    OPTICK_EVENT();

    // Spawn entities
    for (int i = 0; i < Chunk::size; i++) {
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

void MapGenerator::generateChunk(Chunk& chunk, glm::ivec2 pos) {
    auto& configurations = reg.ctx<Settings>().map_configurations;
    generateTerrain(chunk, pos, 0, configurations);
    finalizeChunk(chunk, pos);
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
