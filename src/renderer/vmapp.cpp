#include "vmapp.h"

#include "device.h"

VmaBuffer::VmaBuffer() : device(nullptr) {
    
}

VmaBuffer::VmaBuffer(Device& device, VmaAllocationCreateInfo* allocInfo, const vk::BufferCreateInfo& bufferInfo) : device(&device) {
    
    VmaAllocationInfo info;
    
    VkResult result = vmaCreateBuffer(device, reinterpret_cast<const VkBufferCreateInfo*> (&bufferInfo), allocInfo, reinterpret_cast<VkBuffer*> (&buffer), &allocation, &info);
    
    if(result != VK_SUCCESS) {
        if(result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
            std::cout << "Allocation failed : Out of device memory" << std::endl;
        } else if(result == VK_ERROR_OUT_OF_HOST_MEMORY) {
            std::cout << "Allocation failed : Out of host memory" << std::endl;
        } else {
            std::cout << "Vma failed with result : " << result << std::endl;
        }
    }
    
    size = bufferInfo.size;
    offset = info.offset;
    memory = info.deviceMemory;
    
}

VmaBuffer::VmaBuffer(Device& device, const vk::BufferCreateInfo& bufferInfo) {
    
    buffer = device->createBuffer(bufferInfo);
    vk::MemoryRequirements memreq = device->getBufferMemoryRequirements(buffer);
    memory = device->allocateMemory(vk::MemoryAllocateInfo(memreq.size, device.getMemoryType(memreq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
    device->bindBufferMemory(buffer, memory, 0);
    
    size = bufferInfo.size;
    offset = 0;
    
}

VmaBuffer::~VmaBuffer() {
    
    if(device != nullptr) {
        
        if(allocation != nullptr) {
            vmaDestroyBuffer(*device, static_cast<VkBuffer>(buffer), allocation);
        } else {
            (*device)->destroy(buffer);
            (*device)->free(memory);
        }
        
    }
    
}


VmaImage::VmaImage() : device(nullptr) {
    
}

VmaImage::VmaImage(Device& device, VmaAllocationCreateInfo* allocInfo, const vk::ImageCreateInfo& imageInfo) : device(&device) {
    
    VmaAllocationInfo info;
    
    VkResult result = vmaCreateImage(device, reinterpret_cast<const VkImageCreateInfo*> (&imageInfo), allocInfo, reinterpret_cast<VkImage*> (&image), &allocation, &info);
    
    if(result != VK_SUCCESS) {
        if(result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
            std::cout << "Allocation failed : Out of device memory" << std::endl;
        } else if(result == VK_ERROR_OUT_OF_HOST_MEMORY) {
            std::cout << "Allocation failed : Out of host memory" << std::endl;
        } else {
            std::cout << "Vma failed with result : " << result << std::endl;
        }
    }
    
    size = info.size;
    offset = info.offset;
    memory = info.deviceMemory;
    
}

VmaImage::~VmaImage() {
    
    if(device != nullptr) {
        
        vmaDestroyImage(*device, static_cast<VkImage>(image), allocation);
        
    }
    
}
