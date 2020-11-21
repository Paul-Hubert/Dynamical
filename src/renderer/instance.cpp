#include "instance.h"

#include "loader.inl"

#include <SDL_vulkan.h>
#include <iostream>
#include <util/util.h>

#include "vr_context.h"

#include "context.h"

#define LOG_LEVEL VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT

bool checkLayers(std::vector<const char *> layerNames) {
    
    std::vector<vk::LayerProperties> availableLayers = vk::enumerateInstanceLayerProperties();
    
    for (const char* layerName : layerNames) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
    
}

bool checkInstanceExtensions(std::vector<const char *> extensionNames) {
    
    std::vector<vk::ExtensionProperties> availableExtensions = vk::enumerateInstanceExtensionProperties();
    
    for (const char* extensionName : extensionNames) {
        bool layerFound = false;

        for (const auto& extensionsProperties : availableExtensions) {
            if (strcmp(extensionName, extensionsProperties.extensionName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
    
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    
    if(messageSeverity < LOG_LEVEL) return VK_FALSE;
    
    if(messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        std::cerr << "    General ";
    } else if(messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        std::cerr << "Performance ";
    } else if(messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        std::cerr << " Validation ";
    }
    
    if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        std::cerr << "Verbose ";
    } else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        std::cerr << "Info    ";
    } else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "Warning ";
    } else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        std::cerr << "Error   ";
    }
    
    std::cerr << pCallbackData->pMessage << std::endl;
    
    if(messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << std::endl;
    }
    
    return VK_FALSE;
}

Instance::Instance(Context& ctx) : ctx(ctx) {
    
    uint32_t extensionCount;
    SDL_Vulkan_GetInstanceExtensions(ctx.win, &extensionCount, nullptr);
    std::vector<const char *> extensionNames(extensionCount);
    SDL_Vulkan_GetInstanceExtensions(ctx.win, &extensionCount, extensionNames.data());



    uint32_t size = 0;
    PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR = nullptr;
    xrCheckResult(xrGetInstanceProcAddr(ctx.vr.instance, "xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction*)&xrGetVulkanInstanceExtensionsKHR));

    xrCheckResult(xrGetVulkanInstanceExtensionsKHR(ctx.vr.instance, ctx.vr.system_id, 0, &size, nullptr));

    std::vector<char> buf(size);
    xrCheckResult(xrGetVulkanInstanceExtensionsKHR(ctx.vr.instance, ctx.vr.system_id, size, &size, buf.data()));
    if(buf[0] != '\0') extensionNames.push_back(buf.data());
    for(int i = 0; i<buf.size(); i++) {
        if(buf[i] == ' ') {
            buf[i] = '\0';
            extensionNames.push_back(&buf[i+1]);
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

    std::vector<const char *> layerNames {};

#ifndef NDEBUG
    extensionNames.push_back("VK_EXT_debug_utils");
    layerNames.push_back("VK_LAYER_KHRONOS_validation");
    if(!checkLayers(layerNames)) {
        std::vector<vk::LayerProperties> availables = vk::enumerateInstanceLayerProperties();

        for (vk::LayerProperties available : availables) {
            std::cout << available.layerName << "\n";
        }

        throw std::runtime_error("Validation layers requested, but not available!");
    }
#endif
    
    if(!checkInstanceExtensions(extensionNames)) {
        
        std::vector<vk::ExtensionProperties> availables = vk::enumerateInstanceExtensionProperties();
        
        for (vk::ExtensionProperties available : availables) {
            std::cout << available.extensionName << "\n";
        }
        
        throw std::runtime_error("Instance Extensions requested, but not available!");
    }

    instance = vk::createInstance(vk::InstanceCreateInfo({}, &appInfo, layerNames.size(), layerNames.data(), extensionNames.size(), extensionNames.data()));

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
