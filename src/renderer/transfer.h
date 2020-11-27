#ifndef TRANSFER_H
#define TRANSFER_H

#include <array>
#include <renderer/vk.h>

#include <memory>

#include "vmapp.h"

#include "model/bufferc.h"
#include "model/imagec.h"

class Context;

class Transfer {
public:
    Transfer(Context& ctx);
    void flush();
    vk::CommandBuffer getCommandBuffer();
    
    std::shared_ptr<ImageC> createImage(const void* data, size_t real_size, vk::ImageCreateInfo info, vk::ImageLayout layout);
    std::shared_ptr<BufferC> createBuffer(const void* data, vk::BufferCreateInfo info);
    
    ~Transfer();
private:
    Context& ctx;
    
    vk::CommandPool pool;

    struct Upload {
        void reset(Context& ctx, vk::CommandPool pool);
        vk::CommandBuffer command;
        vk::Fence fence;
        std::vector<VmaBuffer> stagingBuffers;

        std::vector<std::shared_ptr<BufferC>> uploaded_buffers;
        std::vector<std::shared_ptr<ImageC>> uploaded_images;
    };

    std::vector<Upload> uploads;
    Upload current;
    
    bool empty = true;
    
};

#endif
