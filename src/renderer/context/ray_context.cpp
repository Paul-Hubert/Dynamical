#include "ray_context.h"

#include <cstring>

#include "util/log.h"

using namespace dy;

namespace {

const char* const ray_extensions[] = {
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_QUERY_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
};

}

void RayContext::detect(vk::PhysicalDevice physical, uint32_t instance_version, bool wanted) {

    on = false;
    if(!wanted) {
        dy::log(dy::info) << "Ray tracing : off (ray_tracing=false)\n";
        return;
    }

    if(instance_version < VK_API_VERSION_1_2 || physical.getProperties().apiVersion < VK_API_VERSION_1_2) {
        dy::log(dy::info) << "Ray tracing : no Vulkan 1.2, lighting without rays\n";
        return;
    }

    auto available = physical.enumerateDeviceExtensionProperties();
    for(const char* name : ray_extensions) {
        bool found = false;
        for(auto& extension : available) {
            if(std::strcmp(name, extension.extensionName) == 0) found = true;
        }
        if(!found) {
            dy::log(dy::info) << "Ray tracing : the GPU has no " << name << ", lighting without rays\n";
            return;
        }
    }

    auto has = physical.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayQueryFeaturesKHR>();
    auto& v12 = has.get<vk::PhysicalDeviceVulkan12Features>();
    bool features = v12.bufferDeviceAddress && v12.descriptorIndexing && v12.runtimeDescriptorArray
        && v12.shaderSampledImageArrayNonUniformIndexing && v12.descriptorBindingPartiallyBound && v12.scalarBlockLayout
        && has.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure
        && has.get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery;
    if(!features) {
        dy::log(dy::info) << "Ray tracing : the GPU lacks a feature of the rays, lighting without them\n";
        return;
    }

    auto properties = physical.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
    scratch_alignment = std::max(1u, properties.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>().minAccelerationStructureScratchOffsetAlignment);

    on = true;
    dy::log(dy::info) << "Ray tracing : acceleration structures and ray queries\n";

}

std::vector<const char*> RayContext::extensions() const {
    if(!on) return {};
    return std::vector<const char*>(std::begin(ray_extensions), std::end(ray_extensions));
}

const void* RayContext::features(const vk::PhysicalDeviceFeatures& base) {

    features12 = vk::PhysicalDeviceVulkan12Features();
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;
    features12.runtimeDescriptorArray = true;
    features12.shaderSampledImageArrayNonUniformIndexing = true;
    features12.descriptorBindingPartiallyBound = true;
    features12.scalarBlockLayout = true;

    acceleration = vk::PhysicalDeviceAccelerationStructureFeaturesKHR();
    acceleration.accelerationStructure = true;

    query = vk::PhysicalDeviceRayQueryFeaturesKHR();
    query.rayQuery = true;

    features2 = vk::PhysicalDeviceFeatures2(base);
    features2.pNext = &features12;
    features12.pNext = &acceleration;
    acceleration.pNext = &query;

    return &features2;

}
