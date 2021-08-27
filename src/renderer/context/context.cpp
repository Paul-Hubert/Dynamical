#include "context.h"

Context::Context(entt::registry& reg) : win(*this, reg), instance(*this, reg), device(*this, reg), transfer(*this, reg), swap(*this, reg), classic_render(*this, reg) {

    //OPTICK_GPU_INIT_VULKAN(reinterpret_cast<VkDevice*> (&device.logical), reinterpret_cast<VkPhysicalDevice*> (&device.physical), reinterpret_cast<VkQueue*> (&device.graphics), &device.g_i, 1, nullptr);

    reg.set<Context*>(this);

    transfer.flush();

}

Context::~Context() {
    
    
    
}
