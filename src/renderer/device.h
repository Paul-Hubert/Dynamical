#ifndef DEVICE_H
#define DEVICE_H

#include <vulkan/vulkan.hpp>
#include "vma/vk_mem_alloc.h"

class Instance;

class Device {
public :
    Device(Instance &inst);
    ~Device();
    
    vk::Device* operator->() {return &logical;}
    operator vk::Device() { return logical; }
    operator VkDevice() { return static_cast<VkDevice>(logical); }
    operator vk::PhysicalDevice() { return physical; }
    operator VkPhysicalDevice() { return static_cast<VkPhysicalDevice>(physical); }
    operator VmaAllocator() { return allocator; }
    
    uint32_t getScore(vk::PhysicalDevice &device);
    uint32_t getMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags properties);
    
    vk::Queue graphics, compute, transfer;
    uint32_t g_i = 0, c_i = 0, t_i = 0;
    
    vk::PhysicalDeviceFeatures requiredFeatures;
    std::vector<const char*> requiredExtensions;
    
    vk::PhysicalDeviceProperties properties;
    vk::PhysicalDeviceFeatures features;
    vk::PhysicalDeviceMemoryProperties memoryProperties;
    std::vector<vk::QueueFamilyProperties> queueFamilies;
    std::vector<vk::ExtensionProperties> extensions;
    
    vk::PhysicalDevice physical;
    vk::Device logical;
    VmaAllocator allocator;
    
private:
    Instance &instance;
};

#endif