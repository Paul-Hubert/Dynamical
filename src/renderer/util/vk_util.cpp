#include "vk_util.h"

#include "util/util.h"

#define LOG_LEVEL VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
#define XR_LOG_LEVEL XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT

#ifndef NDEBUG

bool debugOutput = true;

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    if(!debugOutput)
        return VK_FALSE;

    if(messageSeverity < LOG_LEVEL)
        return VK_FALSE;

    if(pCallbackData->messageIdNumber == 0x6bbb14
        || pCallbackData->messageIdNumber == 0xe92b452d
        || pCallbackData->messageIdNumber == 0x4dae5635
        || pCallbackData->messageIdNumber == 0x71500fba)
        return VK_FALSE;

    /*
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
    */

    std::cerr << pCallbackData->pMessage << std::endl;

    if(messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << std::endl;
    }

    return VK_FALSE;
}





XRAPI_ATTR XrBool32 XRAPI_CALL debugCallback(XrDebugUtilsMessageSeverityFlagsEXT messageSeverity, XrDebugUtilsMessageTypeFlagsEXT messageType, const XrDebugUtilsMessengerCallbackDataEXT* msg, void* user_data) {

    if(messageSeverity < XR_LOG_LEVEL) return XR_FALSE;

    /*
    if(messageType == XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        std::cerr << "    General ";
    } else if(messageType == XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        std::cerr << "Performance ";
    } else if(messageType == XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        std::cerr << " Validation ";
    }

    if(messageSeverity == XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        std::cerr << "Verbose ";
    } else if(messageSeverity == XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        std::cerr << "Info    ";
    } else if(messageSeverity == XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "Warning ";
    } else if(messageSeverity == XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        std::cerr << "Error   ";
    }
    */

    std::cerr << msg->message << std::endl;

    if(messageSeverity >= XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << std::endl;
    }

    return XR_FALSE;
}

#endif

void vkCheckResult(VkResult result) {
    if(result != VK_SUCCESS) {
        Util::log(Util::error) << "VK Error : " << result << "\n";
    }
}

void xrCheckResult(XrResult result) {
    if(result != XR_SUCCESS) {
        Util::log(Util::error) << "XR Error : " << result << "\n";
    }
}

std::vector<const char*> checkLayers(std::vector<const char*> layers, std::vector<vk::LayerProperties> availableLayers) {

    std::vector<const char*> out_layers;

    for(const char* layerName : layers) {
        bool layerFound = false;

        for(const auto& layerProperties : availableLayers) {
            if(strcmp(layerName, layerProperties.layerName) == 0) {
                out_layers.push_back(layerName);
                layerFound = true;
                break;
            }
        }

        if(!layerFound) {
            Util::log(Util::warning) << "Layer requested, but not available : " << layerName << "\n";
        }
    }

    return out_layers;

}

std::vector<const char*> checkExtensions(std::vector<const char*> extensions, std::vector<vk::ExtensionProperties> availableExtensions) {

    std::vector<const char*> out_extensions;

    for(const char* extensionName : extensions) {
        bool layerFound = false;

        for(const auto& extensionsProperties : availableExtensions) {
            if(strcmp(extensionName, extensionsProperties.extensionName) == 0) {
                out_extensions.push_back(extensionName);
                layerFound = true;
                break;
            }
        }

        if(!layerFound) {
            Util::log(Util::warning) << "Instance Extension requested, but not available : " << extensionName << "\n";
        }
    }

    return out_extensions;

}

std::vector<const char*> checkLayers(std::vector<const char*> layers, std::vector<XrApiLayerProperties> availableLayers) {

    std::vector<const char*> out_layers;

    for(const char* layerName : layers) {
        bool layerFound = false;

        for(const auto& layerProperties : availableLayers) {
            if(strcmp(layerName, layerProperties.layerName) == 0) {
                out_layers.push_back(layerName);
                layerFound = true;
                break;
            }
        }

        if(!layerFound) {
            Util::log(Util::warning) << "OpenXR Layer requested, but not available : " << layerName << "\n";
        }
    }

    return out_layers;

}

std::vector<const char*> checkExtensions(std::vector<const char*> extensions, std::vector<XrExtensionProperties> availableExtensions) {

    std::vector<const char*> out_extensions;

    for(const char* extensionName : extensions) {
        bool layerFound = false;

        for(const auto& extensionsProperties : availableExtensions) {
            if(strcmp(extensionName, extensionsProperties.extensionName) == 0) {
                out_extensions.push_back(extensionName);
                layerFound = true;
                break;
            }
        }

        if(!layerFound) {
            Util::log(Util::warning) << "OpenXR Extension requested, but not available : " << extensionName << "\n";
        }
    }

    return out_extensions;

}