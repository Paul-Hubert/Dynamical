#include "compute.h"

#include "context.h"

#include "util/util.h"

#include <limits>

using namespace dy;

Compute::Compute(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg), per_frame(NUM_FRAMES) {
    
    pool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer, ctx.device.c_i));

    for(int i = 0; i< per_frame.size(); i++) {
        auto &f = per_frame[i];

        f.fence = ctx.device->createFence({vk::FenceCreateFlagBits::eSignaled});
        f.command = ctx.device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(pool, vk::CommandBufferLevel::ePrimary, 1))[0];

    }

    current = per_frame[index].command;
    current.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

}

bool Compute::flush(std::vector<vk::Semaphore> waits, std::vector<vk::PipelineStageFlags> stages, std::vector<vk::Semaphore> signals) {

    OPTICK_EVENT();
    
    if(!empty) {

        auto& f = per_frame[index];

        current.end();

        ctx.device->waitForFences({f.fence}, VK_TRUE, std::numeric_limits<uint64_t>::max());

        ctx.device->resetFences({f.fence});

        ctx.device.compute.submit(vk::SubmitInfo(waits.size(), waits.data(), stages.data(), 1, &current, signals.size(), signals.data()), f.fence);

        index = (index+1)%per_frame.size();
        current = per_frame[index].command;

        current.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        
        empty = true;
        
        return true;
    }
    return false;
    
}

vk::CommandBuffer Compute::getCommandBuffer() {
    empty = false;
    return current;
}

Compute::~Compute() {

    for(auto f : per_frame) {
        ctx.device->destroy(f.fence);
    }
    
    ctx.device->destroy(pool);
    
}
