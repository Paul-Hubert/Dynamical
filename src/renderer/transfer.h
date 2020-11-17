#ifndef TRANSFER_H
#define TRANSFER_H

#include <array>
#include <renderer/vk.h>

#include "vmapp.h"

class Context;

class Transfer {
public:
    Transfer(Context& ctx);
    void flush();
    vk::CommandBuffer getCommandBuffer();
    
    void prepareImage(std::string str, VmaImage& image, int num_components, int base_mip, int base_array);
    void prepareImage(const void* data, size_t size, VmaImage& image, vk::Extent3D sizes, int base_mip, int base_array);
    bool prepareBuffer(const void* data, VmaBuffer& image);
    
    ~Transfer();
private:
    Context& ctx;
    
    vk::CommandPool pool;
    std::array<vk::CommandBuffer, 2> commandBuffers;
    vk::Fence fence;
    
    int index = 0;
    bool empty = true;
    
    std::array<std::vector<VmaBuffer>, 2> stagingBuffers;
    
};

#endif
