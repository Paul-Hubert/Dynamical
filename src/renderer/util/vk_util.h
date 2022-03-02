#ifndef VK_UTIL_H
#define VK_UTIL_H

#include <renderer/util/vk.h>
#include <renderer/util/xr.h>

#include <vector>

#include "renderer/context/num_frames.h"

namespace dy {

std::vector<const char*> checkLayers(std::vector<const char*> layers, std::vector<vk::LayerProperties> availableLayers);
std::vector<const char*> checkExtensions(std::vector<const char*> extensions, std::vector<vk::ExtensionProperties> availableExtensions);

std::vector<const char*> checkLayers(std::vector<const char*> layers, std::vector<XrApiLayerProperties> availableLayers);
std::vector<const char*> checkExtensions(std::vector<const char*> extensions, std::vector<XrExtensionProperties> availableExtensions);

#ifndef NDEBUG
extern bool debugOutput;
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

XRAPI_ATTR XrBool32 XRAPI_CALL debugCallback(XrDebugUtilsMessageSeverityFlagsEXT messageSeverity, XrDebugUtilsMessageTypeFlagsEXT messageType, const XrDebugUtilsMessengerCallbackDataEXT* msg, void* user_data);

#endif

void vkCheckResult(VkResult result);

void xrCheckResult(XrResult result);


class Context;

template<typename T>
class PerFrame {
public:
    template<typename... Args>
    PerFrame(Args&&... args) : frames(std::forward<Args>(args)...) {}
    PerFrame() {}

    T& get(uint32_t index) {frames.get(index);}
private:
    std::array<T, NUM_FRAMES> frames;
};


}

#endif
