#include "logic/systems/system_list.h"

#include "taskflow/taskflow.hpp"

#include "logic/components/chunk/chunk_map.h"
#include "logic/components/chunk/chunkc.h"
#include "logic/components/chunk/global_chunk_data.h"
#include "logic/components/chunk/stored_chunk.h"


constexpr double base_level = 70;

static std::future<void> callback;
static tf::Taskflow taskflow;

constexpr int max_threads = 20;

void ChunkGeneratorSys::init() {
    
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
                reg.emplace<entt::tag<"loaded"_hs>>(entity);
            }
            reg.clear<GeneratingChunk>();
            
            taskflow.clear();
            current = 0;
            
        } else {
            return;
        }
    }


    {
        auto view = reg.view < GlobalChunkC, entt::tag<"loading"_hs>>();
        for (auto entity : view) {
            GlobalChunkC &chunk = view.get<GlobalChunkC>(entity);
            if (current < max_threads && !reg.has<GeneratingChunk>(entity)) {
                reg.emplace<GeneratingChunk>(entity, chunk);
                reg.emplace<StoredChunk>(entity);
                reg.emplace<GlobalChunkEmpty>(entity, 0.f);
                current++;
            }
        }
    }

    {
        auto view = reg.view<StoredChunk, GlobalChunkEmpty, GeneratingChunk>();
        taskflow.for_each(view.begin(), view.end(), [view](const entt::entity entity) {

            auto &chunk = view.get<GeneratingChunk>(entity).chunk;
            auto &chunk_mean = view.get<GlobalChunkEmpty>(entity).mean;
            chunk_mean = 0;

            GlobalChunkData chunk_data;


            int index = 0;

            bool sign = 0;

            bool empty = true;

            float sum = 0;
            for (int x = 0; x < chunk::max_num_values.x; x++) {
                for (int z = 0; z < chunk::max_num_values.z; z++) {
                    for (int y = 0; y < chunk::max_num_values.y; y++) {

                        int rx = chunk::base_size.x * chunk.pos.x + x * chunk::base_cube_size;
                        int rz = chunk::base_size.z * chunk.pos.z + z * chunk::base_cube_size;
                        int ry = chunk::base_size.y * chunk.pos.y + y * chunk::base_cube_size;

                        float value = base_level - ry + 40 * cos((rx - rz) / 100.) - 52 * sin((rx + rz) / 100.);

                        if (x == 0 && y == 0 && z == 0) {
                            sign = std::signbit(value);
                        } else if (sign != std::signbit(value)) {
                            empty = false;
                        }
                        sum += value;
                        chunk_data.data[index] = value;
                        index++;

                    }
                }
            }

            if (empty) {
                chunk_mean = sum / index;
            } else {
                auto &sparse_chunk = view.get<StoredChunk>(entity);
                sparse_chunk.set(chunk_data);
            }

        });
    }
    
    callback = executor.run(taskflow);
    
}

void ChunkGeneratorSys::finish() {
    
    if(callback.valid()) {
        if(callback.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            std::cerr << "" << std::endl;
        }
    }
    
}
