#ifndef CONTEXT_H
#define CONTEXT_H

#include "windu.h"
#include "instance.h"
#include "device.h"
#include "transfer.h"
#include "swapchain.h"
#include "renderer/classic_render/classic_render.h"

#include "logic/systems/system.h"

#include "entt/entt.hpp"

#include <memory>

namespace dy {

class Context {
public:
    Context(entt::registry& reg);
    ~Context();

    Windu win;
    Instance instance;
    Device device;
    Transfer transfer;
    Swapchain swap;
    ClassicRender classic_render;
    
    uint32_t frame_index;

};

}

#endif
