#ifndef CONTEXT_H
#define CONTEXT_H

#include "vr_context.h"
#include "windu.h"
#include "instance.h"
#include "device.h"
#include "transfer.h"
#include "compute.h"
#include "swapchain.h"

#include "logic/systems/system.h"

#include "entt/entt.hpp"

#include <memory>

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

    // Set 0 of every scene pipeline : the ViewUBO of the desktop camera or of one VR eye
    vk::DescriptorSetLayout view_layout;

    uint32_t frame_index = 0;

};

}

#endif
