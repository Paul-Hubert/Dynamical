#ifndef TRANSFER_H
#define TRANSFER_H

#include <array>
#include "renderer/util/vk.h"

#include <memory>

#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>

class Context;

class Transfer {
public:
    Transfer(Context& ctx, entt::registry& reg);
    bool flush(vk::Semaphore semaphore = nullptr);
    vk::CommandBuffer getCommandBuffer();
    
    std::shared_ptr<VmaImage> createImage(const void* data, size_t real_size, vk::ImageCreateInfo info, vk::ImageLayout layout);
    std::shared_ptr<VmaBuffer> createBuffer(const void* data, vk::BufferCreateInfo info);
    
    void check();
    
    ~Transfer();
    
    
private:
    Context& ctx;
    entt::registry& reg;
    
    vk::CommandPool pool;

    struct Upload {
        void reset(Context& ctx, vk::CommandPool pool);
        vk::CommandBuffer command;
        vk::Fence fence;
        std::vector<VmaBuffer> stagingBuffers;

        std::vector<std::shared_ptr<VmaBuffer>> uploaded_buffers;
        std::vector<std::shared_ptr<VmaImage>> uploaded_images;
    };

    std::vector<Upload> uploads;
    Upload current;
    
    bool empty = true;
    
    
};

#endif
