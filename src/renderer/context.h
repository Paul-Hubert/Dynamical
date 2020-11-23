#ifndef CONTEXT_H
#define CONTEXT_H

#include "windu.h"
#include "instance.h"
#include "device.h"
#include "transfer.h"
#include "swapchain.h"
#include "vr_context.h"

#include "logic/systems/system.h"

#include "entt/entt.hpp"

class Context {
public:
    Context(entt::registry& reg);
    ~Context();

    VRContext vr;
    Windu win;
    Instance instance;
    Device device;
    Transfer transfer;
    Swapchain swap;

};

#endif
