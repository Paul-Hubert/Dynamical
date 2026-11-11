#include "instance.h"

#include "loader.inl"

#include <SDL_vulkan.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <util/util.h>

#include "context.h"

#include "renderer/util/vk_util.h"
#include "logic/settings/settings.h"

using namespace dy;

Instance::Instance(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
    
    uint32_t extensionCount;
    SDL_Vulkan_GetInstanceExtensions(ctx.win, &extensionCount, nullptr);
    std::vector<const char *> extensions(extensionCount);
    SDL_Vulkan_GetInstanceExtensions(ctx.win, &extensionCount, extensions.data());

    std::vector<const char*> layers{};

#ifndef NDEBUG
    extensions.push_back("VK_EXT_debug_utils");
    layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    // Vulkan 1.1 for subgroup operations in the marching cubes shader, 1.2 for the rays of the lighting
    // where the loader has it (RayContext)
    uint32_t version = vk::enumerateInstanceVersion() >= VK_API_VERSION_1_2 ? VK_API_VERSION_1_2 : VK_API_VERSION_1_1;

    // OpenXR decides part of the instance extensions and the api version
    std::vector<std::string> xr_extensions;
    if(ctx.vr.enabled()) {
        xr_extensions = ctx.vr.vulkanInstanceExtensions();
        for(auto& ext : xr_extensions) {
            if(std::find_if(extensions.begin(), extensions.end(), [&](const char* e) { return ext == e; }) == extensions.end()) {
                extensions.push_back(ext.c_str());
            }
        }
        version = ctx.vr.vulkanApiVersion(version);
    }

    layers = checkLayers(layers, vk::enumerateInstanceLayerProperties());
    
    extensions = checkExtensions(extensions, vk::enumerateInstanceExtensionProperties());

    api_version = version;
    vk::ApplicationInfo appInfo("Dynamical", VK_MAKE_VERSION(1, 0, 0), "Dynamical", VK_MAKE_VERSION(1, 0, 0), version);


    instance = vk::createInstance(vk::InstanceCreateInfo({}, &appInfo, (uint32_t) layers.size(), layers.data(), (uint32_t) extensions.size(), extensions.data()));
    
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
    
    Instance &instance = *this;

#ifndef NDEBUG
    auto info = static_cast<VkDebugUtilsMessengerCreateInfoEXT> (vk::DebugUtilsMessengerCreateInfoEXT({},
                                         vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose, 
                                         vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation, 
                                         &debugCallback, nullptr
                                        ));

    INST_LOAD(vkCreateDebugUtilsMessengerEXT);
    
    vkCreateDebugUtilsMessengerEXT(instance, &info, nullptr, &messenger);
#endif
    
    VkSurfaceKHR surf;
    SDL_Vulkan_CreateSurface(ctx.win, instance, &surf);
    ctx.win.surface = surf;
    
    INST_LOAD(vkGetPhysicalDeviceSurfaceSupportKHR);
    this->vkGetPhysicalDeviceSurfaceSupportKHR = vkGetPhysicalDeviceSurfaceSupportKHR;
    
}

bool Instance::supportsPresent(VkPhysicalDevice device, int i) {
    VkBool32 b;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, ctx.win, &b);
    return b == VK_TRUE;
}

Instance::~Instance() {
    
    instance.destroy(ctx.win.surface);

#ifndef NDEBUG
    {
        Instance &instance = *this;
        INST_LOAD(vkDestroyDebugUtilsMessengerEXT);
        
        vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
    }
#endif
    
    instance.destroy();
}
