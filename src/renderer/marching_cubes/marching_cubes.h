#ifndef MARCHING_CUBES_H
#define MARCHING_CUBES_H

#include "vulkan/vulkan.hpp"
#include "vk_mem_alloc.h"

#include "renderer/num_frames.h"

#include "mc_pipeline.h"

#include "entt/entt.hpp"

#include "logic/systems/system.h"

class computing {
public:
    computing(uint32_t index) : index(index) {}
    uint32_t index;
};

constexpr int max_per_frame = 10;

class MarchingCubes : public System {
public:
    MarchingCubes(entt::registry& reg);
    void init() override;
    void tick() override;
    void finish() override;
    const char* name() override {return "Marching Cubes";};
    
private:
    vk::CommandPool commandPool;
    
    vk::QueryPool queryPool;
    
    struct PerFrame {
        vk::CommandBuffer commandBuffer;
        vk::Fence fence;
        bool fence_state = false;
        int chunk_count = 0;
    };
    
    std::array<PerFrame, NUM_FRAMES> per_frame;
    
    std::unique_ptr<MCPipeline> pipeline;
    
};

#endif
