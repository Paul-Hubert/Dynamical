#ifndef VK_DEBUG_H
#define VK_DEBUG_H

#include "vk.h"
#include <string>
#include <cstdio>

namespace dy {

// Helper to extract uint64_t from any vulkan-hpp handle
// This works because vulkan-hpp handles are implicitly convertible to their C counterparts
template<typename HandleT>
inline uint64_t getHandleValue(HandleT handle) {
    // Use implicit conversion to get the underlying C handle, then reinterpret as uint64_t
    return (uint64_t)(size_t)static_cast<void*>((void*)(handle));
}

// Specialize for VkHandles that are already numeric types on 64-bit systems
template<>
inline uint64_t getHandleValue<uint64_t>(uint64_t handle) {
    return handle;
}

/// Set a debug name for a Vulkan object (works with Device and vk::Device)
inline void setObjectName(vk::Device device, vk::ObjectType type, uint64_t handle, const char* name) {
#ifndef NDEBUG
    device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT(type, handle, name));
#endif
}

/// Set a debug name for a Vulkan object using std::string
inline void setObjectName(vk::Device device, vk::ObjectType type, uint64_t handle, const std::string& name) {
    setObjectName(device, type, handle, name.c_str());
}

/// Convenience macro for setting object names
#define SET_VK_NAME(device, type, handle, name) \
    dy::setObjectName(device, type, dy::getHandleValue(handle), name)

/// Convenience macro for setting object names with formatted strings
#define SET_VK_NAME_FMT(device, type, handle, fmt, ...) \
    do { \
        char buf[256]; \
        snprintf(buf, sizeof(buf), fmt, __VA_ARGS__); \
        dy::setObjectName(device, type, dy::getHandleValue(handle), buf); \
    } while(0)

}

#endif
