#include "context.h"

Context::Context(entt::registry& reg) : vr(*this), win(*this), instance(*this), device(*this), transfer(*this), swap(*this) {

    OPTICK_GPU_INIT_VULKAN(reinterpret_cast<VkDevice*> (&device.logical), reinterpret_cast<VkPhysicalDevice*> (&device.physical), reinterpret_cast<VkQueue*> (&device.graphics), &device.g_i, 1, nullptr);

    reg.set<Context*>(this);

    vr.init();

    transfer.flush();

}

Context::~Context() {

    vr.finish();

}
