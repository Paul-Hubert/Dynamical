#ifndef INSTANCE_H
#define INSTANCE_H

#include <renderer/util/vk.h>

#include <entt/entt.hpp>

namespace dy {

class Context;

class Instance {
public:
    Instance(Context& ctx, entt::registry& reg);
    ~Instance();
    vk::Instance* operator->() {return &instance;}
    operator vk::Instance() { return instance; }
    operator VkInstance() { return static_cast<VkInstance>(instance); }
    bool supportsPresent(VkPhysicalDevice device, int i);
#ifndef NDEBUG
    VkDebugUtilsMessengerEXT messenger;
    static bool debugOutput;
#endif
private:
    Context& ctx;
    entt::registry& reg;
    
    vk::Instance instance;
    
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
};

}

#endif
