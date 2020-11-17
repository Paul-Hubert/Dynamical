#ifndef INSTANCE_H
#define INSTANCE_H

#include <renderer/vk.h>

class Windu;

class Instance {
public:
    Instance(Windu& win);
    ~Instance();
    vk::Instance* operator->() {return &instance;}
    operator vk::Instance() { return instance; }
    operator VkInstance() { return static_cast<VkInstance>(instance); }
    bool supportsPresent(VkPhysicalDevice device, int i);
    VkDebugUtilsMessengerEXT messenger;
private:
    vk::Instance instance;
    Windu& win;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
};

#endif
