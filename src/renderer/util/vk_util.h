#ifndef VK_UTIL_H
#define VK_UTIL_H

#include <renderer/util/vk.h>

#include <vector>

#include "renderer/context/num_frames.h"

namespace dy {

std::vector<const char*> checkLayers(std::vector<const char*> layers, std::vector<vk::LayerProperties> availableLayers);
std::vector<const char*> checkExtensions(std::vector<const char*> extensions, std::vector<vk::ExtensionProperties> availableExtensions);

#ifndef NDEBUG
extern bool debugOutput;
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

#endif

void vkCheckResult(VkResult result);

}

#endif
