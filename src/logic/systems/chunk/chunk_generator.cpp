#include "logic/systems/system_list.h"

#include "FastNoiseSIMD/FastNoiseSIMD/FastNoiseSIMD.h"
#include "taskflow/taskflow.hpp"

#include "logic/components/chunk/chunk_map.h"
#include "logic/components/chunk/chunkc.h"
#include "logic/components/chunk/global_chunk_data.h"
#include "logic/components/chunk/stored_chunk.h"

const static std::unique_ptr<FastNoiseSIMD> terrain_noise = std::unique_ptr<FastNoiseSIMD>(FastNoiseSIMD::NewFastNoiseSIMD());
const static std::unique_ptr<FastNoiseSIMD> cave_noise = std::unique_ptr<FastNoiseSIMD>(FastNoiseSIMD::NewFastNoiseSIMD(2010));
constexpr double frequency = 0.001;
constexpr double amplitude = 500.;
constexpr int octaves = 4;

constexpr double base_level = 70;

static std::future<void> callback;
static tf::Taskflow taskflow;

constexpr int max_threads = 20;

void ChunkGeneratorSys::init() {
    
    terrain_noise->SetFractalOctaves(octaves);
    terrain_noise->SetFrequency(frequency);
    cave_noise->SetFractalOctaves(octaves-1);
    cave_noise->SetFrequency(frequency);
    
}

void ChunkGeneratorSys::tick() {
    
    struct GeneratingChunk {
        GlobalChunkC chunk;
    };
    
    tf::Executor& executor = reg.ctx<tf::Executor>();
    
    static int current = 0;
    if(callback.valid()) {
        if(callback.wait_for(std::chrono::nanoseconds(0)) == std::future_status::ready) {
            
            auto endView = reg.view<GlobalChunkEmpty, entt::tag<"loading"_hs>>();
            for(auto entity : endView) {
                if(endView.get<GlobalChunkEmpty>(entity).mean != 0) {
                    reg.remove<StoredChunk>(entity);
                } else {
                    reg.remove<GlobalChunkEmpty>(entity);
                }
                reg.remove<entt::tag<"loading"_hs>>(entity);
                reg.assign<entt::tag<"loaded"_hs>>(entity);
            }
            reg.reset<GeneratingChunk>();
            
            taskflow.clear();
            current = 0;
            
        } else {
            return;
        }
    }
    
    reg.view<GlobalChunkC, entt::tag<"loading"_hs>>().each([this](const entt::entity entity, GlobalChunkC& chunk, auto) {
        if(current < max_threads && !reg.has<GeneratingChunk>(entity)) {
            reg.assign<GeneratingChunk>(entity, chunk);
            reg.assign<StoredChunk>(entity);
            reg.assign<GlobalChunkEmpty>(entity, 0.f);
            current++;
        }
    });
    
    
    auto view = reg.view<StoredChunk, GlobalChunkEmpty, GeneratingChunk>();
    taskflow.parallel_for(view.begin(), view.end(), [view](const entt::entity entity) { 
        
        auto& chunk = view.get<GeneratingChunk>(entity).chunk;
        auto& chunk_mean = view.get<GlobalChunkEmpty>(entity).mean;
        chunk_mean = 0;
        
        GlobalChunkData chunk_data;
        
        terrain_noise->FillSimplexFractalSet(chunk_data.data.data(),
            chunk::num_cubes.x * chunk.pos.x - chunk::border * chunk::max_mul,
            chunk::num_cubes.z * chunk.pos.z - chunk::border * chunk::max_mul,
            chunk::num_cubes.y * chunk.pos.y - chunk::border * chunk::max_mul,
            chunk::max_num_values.x, chunk::max_num_values.z, chunk::max_num_values.y, chunk::base_cube_size);
        
        float* cav = cave_noise->GetSimplexFractalSet(
            chunk::num_cubes.x * chunk.pos.x - chunk::border * chunk::max_mul,
            chunk::num_cubes.z * chunk.pos.z - chunk::border * chunk::max_mul,
            chunk::num_cubes.y * chunk.pos.y - chunk::border * chunk::max_mul,
            chunk::max_num_values.x, chunk::max_num_values.z, chunk::max_num_values.y, chunk::base_cube_size);
        
        
        int index = 0;
        
        bool sign = 0;
        
        bool empty = true;
        
        float sum = 0;
        for(int x = 0; x < chunk::max_num_values.x; x++) {
            for(int z = 0; z < chunk::max_num_values.z; z++) {
                for(int y = 0; y < chunk::max_num_values.y; y++) {
                    
                    int rx = chunk::base_size.x * chunk.pos.x + x * chunk::base_cube_size;
                    int rz = chunk::base_size.z * chunk.pos.z + z * chunk::base_cube_size;
                    int ry = chunk::base_size.y * chunk.pos.y + y * chunk::base_cube_size;
                    
                    float value = std::min(base_level - ry + amplitude * chunk_data.data[index], base_level - Util::s_sq(150.*cav[index]-50.));
                    
                    if(x == 0 && y == 0 && z == 0) {
                        sign = std::signbit(value);
                    } else if(sign != std::signbit(value)) {
                        empty = false;
                    }
                    sum += value;
                    chunk_data.data[index] = value;
                    index++;
                    
                }
            }
        }
        
        FastNoiseSIMD::FreeNoiseSet(cav);
        
        if(empty) {
            chunk_mean = sum/index;
        } else {
            auto& sparse_chunk = view.get<StoredChunk>(entity);
            sparse_chunk.set(chunk_data);
        }
        
    });
    
    callback = executor.run(taskflow);
    
}

void ChunkGeneratorSys::finish() {
    
    if(callback.valid()) {
        if(callback.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            std::cerr << "" << std::endl;
        }
    }
    
}
