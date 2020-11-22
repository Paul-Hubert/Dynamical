#ifndef CHUNK_RENDER_H
#define CHUNK_RENDER_H

#include "chunk_pipeline.h"
#include "grass_pipeline.h"

#include "entt/entt.hpp"

class Transfer;
class Device;
class Swapchain;
class Renderpass;
class UBODescriptor;

class ChunkRender {
public:
    ChunkRender(Device& device, Transfer& transfer, Swapchain& swap, Renderpass& renderpass, UBODescriptor& ubo);
    void render(entt::registry& reg, vk::CommandBuffer commandBuffer, vk::DescriptorSet set);
    ~ChunkRender();
    
private:
    ChunkPipeline chunk_pipeline;
    GrassPipeline grass_pipeline;
    Device& device;
    Transfer& transfer;
    Swapchain& swap;
    Renderpass& renderpass;
    
};

#endif
