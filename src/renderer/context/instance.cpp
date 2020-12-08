#include "instance.h"

#include "loader.inl"

#include <SDL_vulkan.h>
#include <iostream>
#include <util/util.h>

#include "vr_context.h"

#include "context.h"

#include "renderer/util/vk_util.h"

Instance::Instance(Context& ctx) : ctx(ctx) {
    
    uint32_t extensionCount;
    SDL_Vulkan_GetInstanceExtensions(ctx.win, &extensionCount, nullptr);
    std::vector<const char *> extensions(extensionCount);
    SDL_Vulkan_GetInstanceExtensions(ctx.win, &extensionCount, extensions.data());

    std::vector<const char*> layers{};

#ifndef NDEBUG
    extensions.push_back("VK_EXT_debug_utils");
    layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    layers = checkLayers(layers, vk::enumerateInstanceLayerProperties());

    extensions = checkExtensions(extensions, vk::enumerateInstanceExtensionProperties());

    {
        uint32_t size = 0;
        PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR = nullptr;
        xrCheckResult(xrGetInstanceProcAddr(ctx.vr.instance, "xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction*)&xrGetVulkanInstanceExtensionsKHR));

        xrCheckResult(xrGetVulkanInstanceExtensionsKHR(ctx.vr.instance, ctx.vr.system_id, 0, &size, nullptr));

        char* buf = (char*)malloc(size * sizeof(char));
        xrCheckResult(xrGetVulkanInstanceExtensionsKHR(ctx.vr.instance, ctx.vr.system_id, size, &size, buf));

        if(buf[0] != '\0') extensions.push_back(buf);
        for(int i = 0; buf[i] != '\0'; i++) {
            if(buf[i] == ' ') {
                buf[i] = '\0';
                extensions.push_back(&buf[i + 1]);
            }
        }

    }

    PFN_xrGetVulkanGraphicsRequirementsKHR xrGetVulkanGraphicsRequirementsKHR = nullptr;
    xrCheckResult(xrGetInstanceProcAddr(ctx.vr.instance, "xrGetVulkanGraphicsRequirementsKHR", (PFN_xrVoidFunction *)(&xrGetVulkanGraphicsRequirementsKHR)));

    XrGraphicsRequirementsVulkanKHR requirement = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
    xrCheckResult(xrGetVulkanGraphicsRequirementsKHR(ctx.vr.instance, ctx.vr.system_id, &requirement));

    uint32_t version = VK_MAKE_VERSION(1,1,0);
    version = VK_MAKE_VERSION(std::min(std::max(VK_VERSION_MAJOR(version), (uint32_t) (XR_VERSION_MAJOR(requirement.minApiVersionSupported))), (uint32_t) (XR_VERSION_MAJOR(requirement.maxApiVersionSupported))),
                              std::min(std::max(VK_VERSION_MINOR(version), (uint32_t) (XR_VERSION_MINOR(requirement.minApiVersionSupported))), (uint32_t) (XR_VERSION_MINOR(requirement.maxApiVersionSupported))),
                              std::min(std::max(VK_VERSION_PATCH(version), (uint32_t) (XR_VERSION_PATCH(requirement.minApiVersionSupported))), (uint32_t) (XR_VERSION_PATCH(requirement.maxApiVersionSupported))));



    vk::ApplicationInfo appInfo("Test", VK_MAKE_VERSION(1, 0, 0), "Dynamical", VK_MAKE_VERSION(1, 0, 0), version);


    instance = vk::createInstance(vk::InstanceCreateInfo({}, &appInfo, (uint32_t) layers.size(), layers.data(), (uint32_t) extensions.size(), extensions.data()));

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
