#ifndef VMA_H
#define VMA_H

#include "vk_mem_alloc.h"
#include "renderer/vk.h"

#include <iostream>

class Device;

class VmaBuffer {
public:
    
    VmaBuffer();
    
    VmaBuffer(Device& device, VmaAllocationCreateInfo* allocInfo, const vk::BufferCreateInfo& bufferInfo);
    VmaBuffer(Device& device, const vk::BufferCreateInfo& bufferInfo);
    VmaBuffer(VmaBuffer&& buffer) {
        this->device = buffer.device;
        this->allocation = buffer.allocation;
        this->buffer = buffer.buffer;
        this->size = buffer.size;
        this->offset = buffer.offset;
        this->memory = buffer.memory;
        buffer.device = nullptr;
    };
    
    ~VmaBuffer();
    
    operator VmaAllocation() { return allocation; };
    operator vk::Buffer() { return buffer; };
    VmaBuffer& operator= (VmaBuffer&& buffer) {
        this->~VmaBuffer();
        this->device = buffer.device;
        this->allocation = buffer.allocation;
        this->buffer = buffer.buffer;
        this->size = buffer.size;
        this->offset = buffer.offset;
        this->memory = buffer.memory;
        buffer.device = nullptr;
        return *this;
    };
    operator bool() {
        return device != nullptr;
    }
    
    VmaAllocation allocation;
    vk::Buffer buffer;
    
    vk::DeviceSize size;
    vk::DeviceSize offset;
    vk::DeviceMemory memory;
    
private:
    Device* device;
};

class VmaImage {
public:
    
    VmaImage();
    
    VmaImage(Device& device, VmaAllocationCreateInfo* allocInfo, const vk::ImageCreateInfo& imageInfo);
    VmaImage(VmaImage&& image) {
        this->device = image.device;
        this->allocation = image.allocation;
        this->image = image.image;
        this->size = image.size;
        this->offset = image.offset;
        this->memory = image.memory;
        this->format = image.format;
        image.device = nullptr;
    }
    
    ~VmaImage();
    
    operator VmaAllocation() { return allocation; }
    operator vk::Image() { return image; }
    VmaImage& operator= (VmaImage&& image) {
        this->~VmaImage();
        this->device = image.device;
        this->allocation = image.allocation;
        this->image = image.image;
        this->size = image.size;
        this->offset = image.offset;
        this->memory = image.memory;
        this->format = image.format;
        image.device = nullptr;
        return *this;
    }
    operator bool() {
        return device != nullptr;
    }
    
    VmaAllocation allocation;
    vk::Image image;
    
    vk::DeviceSize size;
    vk::DeviceSize offset;
    vk::DeviceMemory memory;

    vk::Format format;
    
private:
    Device* device;
};

namespace dy {
    
    inline VmaBuffer make_buffer(Device& device, VmaAllocationCreateInfo* allocInfo, const vk::BufferCreateInfo& bufferInfo) {
        return VmaBuffer(device, allocInfo, bufferInfo);
    }
    
    inline VmaImage make_image(Device& device, VmaAllocationCreateInfo* allocInfo, const vk::ImageCreateInfo& imageInfo) {
        return VmaImage(device, allocInfo, imageInfo);
    }
    
    inline VmaBuffer make_buffer_not_vma(Device& device, VmaAllocationCreateInfo*, const vk::BufferCreateInfo& bufferInfo) {
        return VmaBuffer(device, bufferInfo);
    }
    
};

#endif
