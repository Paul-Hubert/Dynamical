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

    // Sizes of a descriptor pool that holds `sets` sets of view_layout
    std::vector<vk::DescriptorPoolSize> viewPoolSizes(uint32_t sets) const;

    // Everything of a set of view_layout but its ViewUBO. The set must be free : the one of a frame whose
    // fence was waited for. Bindings that never change are written the first time only
    void writeViewSet(vk::DescriptorSet set, uint32_t frame_index, bool first);

    uint32_t frame_index = 0;

};

}

#endif
