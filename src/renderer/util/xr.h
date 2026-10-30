#ifndef RENDERER_XR_H
#define RENDERER_XR_H

#include "vk.h"

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <string>
#include <vector>

namespace dy {

// Logs failures, returns true on success so calls can be chained in conditions
bool xrCheckResult(XrResult result, const char* what = nullptr);

std::vector<const char*> checkXrLayers(std::vector<const char*> layers, const std::vector<XrApiLayerProperties>& available);
std::vector<const char*> checkXrExtensions(std::vector<const char*> extensions, const std::vector<XrExtensionProperties>& available);

// Splits the space separated extension lists returned by XR_KHR_vulkan_enable
std::vector<std::string> splitExtensionString(const std::string& list);

}

#endif
