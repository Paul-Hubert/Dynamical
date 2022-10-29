#include "device.h"

#include "loader.inl"
#include "util/util.h"
#include "context.h"

#include <iostream>
#include <set>
#include <string>

#include "renderer/util/vk_util.h"
#include "logic/settings.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

using namespace dy;

bool checkDeviceExtensions(std::vector<const char*> extensionNames, std::vector<vk::ExtensionProperties> availableExtensions) {

    for(const char* extensionName : extensionNames) {
        bool layerFound = false;

        for(const auto& extensionsProperties : availableExtensions) {
            if(strcmp(extensionName, extensionsProperties.extensionName) == 0) {
                layerFound = true;
                break;
            }
        }

        if(!layerFound) {
            return false;
        }
    }

    return true;

}

Device::Device(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {
    
    auto& settings = reg.ctx<Settings>();
    
    vk::PhysicalDeviceFeatures requiredFeatures = vk::PhysicalDeviceFeatures();
    //requiredFeatures.samplerAnisotropy = true;
    //requiredFeatures.shaderStorageImageMultisample = true;
    // HERE : enable needed features (if present in 'features')

    std::vector<const char*> extensions;

    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    std::vector<vk::PhysicalDevice> p_devices = ctx.instance->enumeratePhysicalDevices();

    physical = p_devices[0];

    extensions = checkExtensions(extensions, physical.enumerateDeviceExtensionProperties());
    
    // Prepare queue choice data : GRAPHICS / COMPUTE / TRANSFER
    uint32_t countF = 0;
    
    std::vector<float> priorities(3); priorities[0] = 0.0f; priorities[1] = 0.0f; priorities[2] = 0.0f; 
    
    std::vector<vk::DeviceQueueCreateInfo> pqinfo(3); // Number of queues
    
    std::vector<vk::QueueFamilyProperties> queueFamilies = physical.getQueueFamilyProperties();
    
    
    // Gets the first available queue family that supports graphics and presentation
    g_i = 1000;
    for(uint32_t i = 0; i < queueFamilies.size(); i++) {
        if(queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics && ctx.instance.supportsPresent(static_cast<VkPhysicalDevice> (physical), i)) {
            g_i = i;
            countF++;
            pqinfo[0] = {{}, i, 1, priorities.data()};
            break;
        }
    }
    
    if(g_i == 1000) {
        throw std::runtime_error("Could not get graphics queue family");
    }
    
    
    // Gets a transfer queue family different from graphics and compute family if possible, then different queue index if possible, else just the same queue.
    c_i = 1000;
    for(uint32_t i = 0; i < queueFamilies.size(); i++) {
        if(queueFamilies[i].queueFlags & vk::QueueFlagBits::eCompute) {
            c_i = i;
            if(c_i != g_i) {
                countF++;
                pqinfo[1] = vk::DeviceQueueCreateInfo({}, i, 1, priorities.data());
                break;
            }
        }
    }
    
    if(c_i == 1000) {
        throw std::runtime_error("Could not get compute queue family");
    }
    
    
    // Gets a transfer queue family different from graphics and compute family if possible, then different queue index if possible, else just the same queue.
    t_i = 1000;
    for(uint32_t i = 0; i < queueFamilies.size(); i++) {
        t_i = i;
        if(t_i != g_i && t_i != c_i) {
            countF++;
            pqinfo[2] = vk::DeviceQueueCreateInfo({}, i, 1, priorities.data());
            break;
        }
    }
    
    if(t_i == 1000) {
        throw std::runtime_error("Could not get transfer queue family");
    }

    // Actually just set every queue to the same one
    t_i = c_i = g_i;

    // Create Device
    
    logical = physical.createDevice(vk::DeviceCreateInfo({}, countF, pqinfo.data(), 0, nullptr, (uint32_t) extensions.size(), extensions.data(), &requiredFeatures));
    
    VULKAN_HPP_DEFAULT_DISPATCHER.init(logical);
    
    Device& device = *this;
    
    
    SET_NAME(vk::ObjectType::eDevice, (VkDevice) logical, main)
    
    graphics = logical.getQueue(g_i, 0);
    
    SET_NAME(vk::ObjectType::eQueue, (VkQueue) graphics, graphics)
    
    if(c_i == g_i) {
        compute = graphics;
    } else {
        compute = logical.getQueue(c_i, 0);
        SET_NAME(vk::ObjectType::eQueue, (VkQueue) compute, compute)
    }
    
    if(t_i == g_i) {
        transfer = graphics;
    } else if(t_i == c_i) {
        transfer = compute;
    } else {
        transfer = logical.getQueue(t_i, 0);
        SET_NAME(vk::ObjectType::eQueue, (VkQueue) transfer, transfer)
    }
    
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physical;
    allocatorInfo.device = logical;
    allocatorInfo.instance = ctx.instance;
    vmaCreateAllocator(&allocatorInfo, &allocator);
    
}


uint32_t Device::getMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags properties) {
    auto memoryProperties = physical.getMemoryProperties();
    for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if((typeBits & 1) == 1) {
            if((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        typeBits >>= 1;
    }
    return 1000;
}

bool Device::isDedicated() {
    auto memoryProperties = physical.getMemoryProperties();
    for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if(
            (memoryProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) == vk::MemoryPropertyFlagBits::eDeviceLocal &&
            (memoryProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible) == vk::MemoryPropertyFlags()
        ) {
            return true;
        }
    }
    return false;
}

Device::~Device() {

    vmaDestroyAllocator(allocator);
    
    logical.destroy();
    
}

