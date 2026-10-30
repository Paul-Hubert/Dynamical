#ifndef COMPUTE_H
#define COMPUTE_H

#include <array>
#include "renderer/util/vk.h"

#include <memory>

#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>

namespace dy {

class Context;

class Compute {
public:
    Compute(Context& ctx, entt::registry& reg);
    void prepare();
    bool flush(std::vector<vk::Semaphore> waits, std::vector<vk::PipelineStageFlags> stages, std::vector<vk::Semaphore> signals);
    vk::CommandBuffer getCommandBuffer();

    bool isEmpty() const { return empty; }

    // Index of the command buffer being recorded, its previous use has completed on the GPU
    uint32_t ringIndex() const { return index; }
    uint32_t ringSize() const { return (uint32_t) per_frame.size(); }

    ~Compute();

private:
    Context& ctx;
    entt::registry& reg;
    
    vk::CommandPool pool;

    struct per_frame {
        vk::CommandBuffer command;
        vk::Fence fence;
    };
    std::vector<per_frame> per_frame;

    uint32_t index = 0;

    vk::CommandBuffer current;

    bool empty = true;
    
};

}

#endif
