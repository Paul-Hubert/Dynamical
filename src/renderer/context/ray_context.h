#ifndef DY_RAY_CONTEXT_H
#define DY_RAY_CONTEXT_H

#include "renderer/util/vk.h"

#include <vector>

namespace dy {

// Ray tracing of the device, for the lighting (docs/lighting.md) : acceleration structures and ray queries.
// Where the GPU has them the device is created with Vulkan 1.2, buffer device addresses and descriptor
// indexing; elsewhere, or with ray_tracing=false, enabled() is false and the engine draws as it did without.
// Nothing else in the renderer asks the device what it can do.
class RayContext {
public:
    // Before the device is created
    void detect(vk::PhysicalDevice physical, uint32_t instance_version, bool wanted);

    bool enabled() const { return on; }

    // What the device is created with when enabled
    std::vector<const char*> extensions() const;
    // The features of the device, `base` and those of the rays : pNext of the vk::DeviceCreateInfo (which
    // then has no pEnabledFeatures)
    const void* features(const vk::PhysicalDeviceFeatures& base);

    // Offsets in a scratch buffer are multiples of this
    uint32_t scratch_alignment = 1;

private:
    bool on = false;

    vk::PhysicalDeviceFeatures2 features2;
    vk::PhysicalDeviceVulkan12Features features12;
    vk::PhysicalDeviceAccelerationStructureFeaturesKHR acceleration;
    vk::PhysicalDeviceRayQueryFeaturesKHR query;
};

}

#endif
