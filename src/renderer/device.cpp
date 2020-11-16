#include "device.h"

#include "instance.h"
#include "loader.inl"
#include "util/util.h"

#include <iostream>
#include <set>
#include <string>

Device::Device(Instance &inst) : instance(inst) {
    
    requiredFeatures = vk::PhysicalDeviceFeatures();
    requiredFeatures.samplerAnisotropy = true;
    // HERE : enable needed features (if present in 'features')
    
    requiredExtensions = {};
    requiredExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    // HERE : enable needed extensions (if present in 'extensions')
    
    std::vector<vk::PhysicalDevice> p_devices = instance->enumeratePhysicalDevices();
    
    // Rate each device and pick the first best in the list, if its score is > 0
    uint32_t index = 1000, max = 0;
    for(uint32_t i = 0; i<p_devices.size(); i++) {
        uint32_t score = getScore(p_devices[i]);
        if(score > max) { // Takes only a score higher than the last (implicitly higher than 0)
            max = score;
            index = i;
        }
    }
    
    if(index == 1000) {  // if no suitable device is found just take down the whole place
        throw std::runtime_error("No suitable vulkan device found. Please check your driver and hardware.");
    }
    
    physical =  p_devices[index]; // Found physical device
    
    
    // Get device properties
    properties = physical.getProperties();
    features = physical.getFeatures();
    memoryProperties = physical.getMemoryProperties();
    
    queueFamilies = physical.getQueueFamilyProperties();
    extensions = physical.enumerateDeviceExtensionProperties();
    
    // Prepare queue choice data : GRAPHICS / COMPUTE / TRANSFER
    uint32_t countF = 0;
    
    std::vector<float> priorities(3); priorities[0] = 0.0f; priorities[1] = 0.0f; priorities[2] = 0.0f; 
    
    std::vector<vk::DeviceQueueCreateInfo> pqinfo(3); // Number of queues
    
    
    
    
    // Gets the first available queue family that supports graphics and presentation
    g_i = 1000;
    for(uint32_t i = 0; i < queueFamilies.size(); i++) {
        if(queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics && instance.supportsPresent(static_cast<VkPhysicalDevice> (physical), i)) {
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
    
    /*
    for(const auto &ext : extensions) {
        if(strcmp(VK_EXT_DEBUG_MARKER_EXTENSION_NAME, ext.extensionName) == 0) requiredExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
    }*/
    
    // Create Device
    
    logical = physical.createDevice(vk::DeviceCreateInfo({}, countF, pqinfo.data(), 0, nullptr, requiredExtensions.size(), requiredExtensions.data(), &requiredFeatures));
    
    
#ifndef NDEBUG
    
    Device& device = *this;
    DEV_LOAD(vkSetDebugUtilsObjectNameEXT)
    this->vkSetDebugUtilsObjectNameEXT = vkSetDebugUtilsObjectNameEXT;
    
    if(isDedicated()) {
        std::cout << "memory is dedicated" << std::endl;
    } else {
        std::cout << "memory is local" << std::endl;
    }
    
#endif
    
    SET_NAME(vk::ObjectType::eDevice, (VkDevice) logical, main)
    
    graphics = logical.getQueue(g_i, 0);
    
    SET_NAME(vk::ObjectType::eQueue, (VkQueue) graphics, graphics)
    
    g_mutex_ = std::make_unique<std::mutex>();
    g_mutex = g_mutex_.get();
    
    if(c_i == g_i) {
        compute = graphics;
        c_mutex = g_mutex_.get();
    } else {
        compute = logical.getQueue(c_i, 0);
        c_mutex_ = std::make_unique<std::mutex>();
        c_mutex = c_mutex_.get();
        SET_NAME(vk::ObjectType::eQueue, (VkQueue) compute, compute)
    }
    
    if(t_i == g_i) {
        transfer = graphics;
        t_mutex = g_mutex_.get();
    } else if(t_i == c_i) {
        transfer = compute;
        t_mutex = c_mutex_.get();
    } else {
        transfer = logical.getQueue(t_i, 0);
        t_mutex_ = std::make_unique<std::mutex>();
        t_mutex = t_mutex_.get();
        SET_NAME(vk::ObjectType::eQueue, (VkQueue) transfer, transfer)
    }
    
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physical;
    allocatorInfo.device = logical;
    vmaCreateAllocator(&allocatorInfo, &allocator);
    
}

uint32_t Device::getScore(vk::PhysicalDevice &device) {
    // Get device properties
    
    uint32_t score = 1;
    
    properties = device.getProperties();
    features = device.getFeatures();
    std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();
    std::vector<vk::ExtensionProperties> extensions = device.enumerateDeviceExtensionProperties();
    
    if(properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) score++; // is a dedicated graphics card
    if(queueFamilies.size() > 1) score ++; // has more than one queue family
    
    std::set<std::string> required(requiredExtensions.begin(), requiredExtensions.end());
    for(const auto &ext : extensions) {
        required.erase(ext.extensionName);
    }
    
    if(!required.empty()) score = 0;
    
    auto prop2 = device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceSubgroupProperties>();
    auto subgroup = prop2.get<vk::PhysicalDeviceSubgroupProperties>();
    
    if(!(subgroup.supportedOperations & vk::SubgroupFeatureFlagBits::eArithmetic && subgroup.supportedOperations & vk::SubgroupFeatureFlagBits::eBallot)) {
        score = 0;
    }
    
    if(!features.samplerAnisotropy) {
        score = 0;
    }
    
    return score;
}

uint32_t Device::getMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags properties) {
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
    
    Util::log(Util::debug) << "fefef\n";

    vmaDestroyAllocator(allocator);
    
    logical.destroy();
    
}

