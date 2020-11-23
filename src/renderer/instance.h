#ifndef INSTANCE_H
#define INSTANCE_H

#include <renderer/vk.h>

class Context;

class Instance {
public:
    Instance(Context& ctx);
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
    vk::Instance instance;
    
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
};

#endif
