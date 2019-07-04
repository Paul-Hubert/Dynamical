#include "vmapp.h"

#include "device.h"

VmaBuffer::VmaBuffer() : device(nullptr) {
    
}

VmaBuffer::VmaBuffer(Device& device, VmaAllocationCreateInfo* allocInfo, const vk::BufferCreateInfo& bufferInfo) : device(&device) {
    
    VmaAllocationInfo info;
    
    VkResult result = vmaCreateBuffer(device, reinterpret_cast<const VkBufferCreateInfo*> (&bufferInfo), allocInfo, reinterpret_cast<VkBuffer*> (&buffer), &allocation, &info);
    
    if(result != VK_SUCCESS) {
        std::cout << "Vma failed with result : " << result << std::endl;
    }
    
    size = info.size;
    offset = info.offset;
    memory = info.deviceMemory;
    
}

VmaBuffer::~VmaBuffer() {
    if(device != nullptr) {
        vmaDestroyBuffer(*device, static_cast<VkBuffer>(buffer), allocation);
    }
}


VmaImage::VmaImage() : device(nullptr) {
    
}

VmaImage::VmaImage(Device& device, VmaAllocationCreateInfo* allocInfo, const vk::ImageCreateInfo& imageInfo) : device(&device) {
    
    VmaAllocationInfo info;
    
    VkResult result = vmaCreateImage(device, reinterpret_cast<const VkImageCreateInfo*> (&imageInfo), allocInfo, reinterpret_cast<VkImage*> (&image), &allocation, &info);
    
    if(result != VK_SUCCESS) {
        std::cout << "Vma failed with result : " << result << std::endl;
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