#include "marching_cubes.h"

#include "renderer/device.h"
#include "terrain.h"
#include "util/util.h"
#include "chunk.h"
#include "logic/components/chunk/chunkc.h"
#include "util/profile.h"
#include "logic/components/chunk/chunkdatac.h"
#include "logic/components/renderinfo.h"

constexpr int timestamp_count = 2;
constexpr int total_timestamp_count = timestamp_count*NUM_FRAMES;

MarchingCubes::MarchingCubes(entt::registry& reg) : System(reg), pipeline(nullptr) {
    
}

void MarchingCubes::init() {
    
    Device& device = *reg.ctx<Device*>();
    
    pipeline = std::make_unique<MCPipeline>(device, reg.ctx<TerrainDescriptorLayout>());
    
    commandPool = device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, device.c_i));
    
    std::vector<vk::CommandBuffer> commandBuffers = device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, NUM_FRAMES));
    
    if(marchingcubesprofiling) queryPool = device->createQueryPool(vk::QueryPoolCreateInfo({}, vk::QueryType::eTimestamp, total_timestamp_count));
    
    for(int i = 0; i<NUM_FRAMES; i++) {
        per_frame[i].commandBuffer = commandBuffers[i];
        per_frame[i].fence = device->createFence(vk::FenceCreateInfo());
    }
    
}

void MarchingCubes::tick() {
    
    Device& device = *reg.ctx<Device*>();
    
    auto& cd = reg.ctx<StagedChunkData>();
    int index = cd.sync;
    
    static float t = 0;
    
    t += 0.1;
    
    for(int i = 0; i<NUM_FRAMES; i++) {
        
        if(per_frame[i].fence_state) {
            
            vk::Result result = device->waitForFences(per_frame[i].fence, VK_TRUE, i != index ? 0 : std::numeric_limits<uint64_t>::max());
            
            if(result != vk::Result::eTimeout) {
                
                device->resetFences({per_frame[i].fence});
                per_frame[i].fence_state = false;
                
                reg.view<computing>().each([&, i](entt::entity entity, computing comp) {
                    uint32_t ind = comp.index;
                    if(ind == i) {
                        reg.remove<computing>(entity);
                        reg.remove<ChunkBuild>(entity);
                        Chunk::mutex.lock();
                        reg.assign<entt::tag<"ready"_hs>>(entity);
                        Chunk::mutex.unlock();
                    }
                });
                
                cd.index[i] = 0;
                
                if(marchingcubesprofiling) {
                    if(per_frame[i].chunk_count > 0) {
                        std::array<uint64_t, timestamp_count> timestamps;
                        device->getQueryPoolResults(queryPool, 0, timestamp_count, timestamps.size() * sizeof(uint64_t), timestamps.data(), sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
                        
                        std::cout << "Chunk rendering took " << (timestamps[timestamps.size()-1] - timestamps[0])*device.properties.limits.timestampPeriod/1000000./per_frame[i].chunk_count << " per chunk for " << per_frame[i].chunk_count << " chunks" << std::endl;
                    }
                }
                
            }
        }
        
    }
    
    if(Chunk::mutex.try_lock()) {
        reg.view<entt::tag<"destroying"_hs>>().each([&](entt::entity entity, auto) {
            
            if(!reg.has<computing>(entity)) {
                reg.destroy(entity);
            }
            
        });
        Chunk::mutex.unlock();
    }
    
    
    per_frame[index].chunk_count = 0;
    
    if(reg.size<ChunkBuild>() > 0) {
        
        vk::CommandBuffer commandBuffer = per_frame[index].commandBuffer;
        
        commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        
        if(marchingcubesprofiling) commandBuffer.resetQueryPool(queryPool, 0, timestamp_count);
        
        if(marchingcubesprofiling) commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, queryPool, 0);
        
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
        
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipeline, 0, {*pipeline}, {});
        
        auto view = reg.view<ChunkC, ChunkBuild, entt::tag<"modified"_hs>>();
        for(auto entity : view) {
            
            auto& chunk = view.get<ChunkC>(entity);
            auto& chonk = view.get<ChunkBuild>(entity);
            
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipeline, 1, {chonk.set}, {static_cast<uint32_t>(sizeof(ChunkData)) * chonk.index});
            
            MCPushConstants pc {chunk.getPosition(), chunk.getCubeSize(), t};
            commandBuffer.pushConstants(*pipeline, vk::ShaderStageFlagBits::eCompute, 0, sizeof(MCPushConstants), &pc);
            
            constexpr glm::ivec3 dispatchSizes(chunk::num_cubes/local_size);
            commandBuffer.dispatch(dispatchSizes.x, dispatchSizes.y, dispatchSizes.z);
            
            reg.assign<computing>(entity, index);
            reg.remove<entt::tag<"modified"_hs>>(entity);
            
            per_frame[index].chunk_count++;
            if(max_per_frame != 0) {
                if(per_frame[index].chunk_count >= max_per_frame) break;
            }

        }
        
        if(marchingcubesprofiling) commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, queryPool, 1);
        
        commandBuffer.end();
        
        if(per_frame[index].chunk_count > 0) {
        
            auto stages = std::vector<vk::PipelineStageFlags> {vk::PipelineStageFlagBits::eTopOfPipe};
            
            device.c_mutex->lock();
            device.compute.submit({vk::SubmitInfo(
                0, nullptr, nullptr,
                1, &commandBuffer,
                0, nullptr
            )}, per_frame[index].fence);
            device.c_mutex->unlock();
            
            per_frame[index].fence_state = true;
            
        }
    }
    
}

void MarchingCubes::finish() {
    
    Device& device = *reg.ctx<Device*>();
    
    device->waitIdle();
    
    for(int i = 0; i<NUM_FRAMES; i++) {
        if(per_frame[i].fence_state) device->waitForFences(per_frame[i].fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
        device->destroy(per_frame[i].fence);
    }
    
    if(profiling) device->destroy(queryPool);
    
    device->destroy(commandPool);
    
}
