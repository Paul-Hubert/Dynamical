#ifndef CONTEXT_H
#define CONTEXT_H

#include "vr_context.h"
#include "windu.h"
#include "instance.h"
#include "device.h"
#include "transfer.h"
#include "compute.h"
#include "swapchain.h"
#include "num_frames.h"

#include "logic/systems/system.h"

#include "entt/entt.hpp"

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dy {

class Context {
public:
    Context(entt::registry& reg);
    ~Context();

    // OpenXR comes first : it picks the Vulkan extensions and physical device
    VRContext vr;
    Windu win;
    Instance instance;
    Device device;
    Transfer transfer;
    Compute compute;
    Swapchain swap;

    // Set 0 of every scene pipeline : the ViewUBO of the desktop camera or of one VR eye (binding 0),
    // the shade of the plants (TreeShade, binding 1), the soil maps of the chunks (SoilMaps, 2) and the
    // lights of the frame (Lights, 3). Their owners keep these up to date
    vk::DescriptorSetLayout view_layout;
    vk::ImageView tree_shade_view;
    vk::Sampler tree_shade_sampler;
    vk::ImageView soil_view;
    vk::Sampler soil_sampler;
    std::array<vk::Buffer, NUM_FRAMES> lights_buffers {};

    // With ray tracing (device.ray.enabled()) the set goes on : what the rays see (RayScene, binding 4)
    vk::AccelerationStructureKHR tlas;
    // What a shader knows of a hit (the geometry table of RayScene, binding 5) and the textures the rays
    // test the leaves against (RayMeshes, binding 6 : only ever added)
    vk::Buffer ray_geometries;
    const std::vector<vk::DescriptorImageInfo>* ray_textures = nullptr;
    static constexpr uint32_t max_ray_textures = 512;

    // The irradiance probes (ProbeVolume) : the table that finds the brick of a chunk (binding 7), and per
    // page of bricks three textures of light (8) and one of the sun and the sky (9). Pages are only ever added
    vk::ImageView probe_index_view;
    vk::Sampler probe_index_sampler;
    const std::vector<vk::DescriptorImageInfo>* probe_light = nullptr;
    const std::vector<vk::DescriptorImageInfo>* probe_direct = nullptr;
    static constexpr uint32_t max_probe_pages = 16;

    // Usage a vertex or an index buffer needs to be traced, nothing without ray tracing
    vk::BufferUsageFlags rayInputUsage() {
        if(!device.ray.enabled()) return {};
        return vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
    }

    // Colour attachments of the render pass the scene passes draw in : the swapchain image, or with ray
    // tracing the colour and the light of the sun apart (SunVisibility)
    uint32_t sceneAttachments() { return device.ray.enabled() ? 2 : 1; }

    // Sizes of a descriptor pool that holds `sets` sets of view_layout
    std::vector<vk::DescriptorPoolSize> viewPoolSizes(uint32_t sets) const;

    // Everything of a set of view_layout but its ViewUBO. The set must be free : the one of a frame whose
    // fence was waited for. Bindings that never change are written the first time only
    void writeViewSet(vk::DescriptorSet set, uint32_t frame_index, bool first);

    uint32_t frame_index = 0;

private:
    // Textures of the rays each set already has
    std::unordered_map<VkDescriptorSet, size_t> ray_textures_written;
    std::unordered_map<VkDescriptorSet, size_t> probe_pages_written;

};

}

#endif
