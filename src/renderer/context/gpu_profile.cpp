#include "gpu_profile.h"

#include "context.h"

#include "util/log.h"

using namespace dy;

GpuProfile::GpuProfile(Context& ctx, entt::registry& reg) : ctx(ctx) {

#ifdef TRACY_ENABLE

    if(!profile::active()) return;

    if(ctx.device.physical.getQueueFamilyProperties()[ctx.device.g_i].timestampValidBits == 0) {
        dy::log(dy::warning) << "Profiler : the graphics queue has no timestamps, no zones on the GPU\n";
        return;
    }

    // Tracy resets its queries and reads the clock of the GPU once, with a command buffer of its own
    vk::CommandPool pool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo(
        vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer, ctx.device.g_i));
    vk::CommandBuffer command = ctx.device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(pool, vk::CommandBufferLevel::ePrimary, 1))[0];

    VkPhysicalDevice physical = ctx.device;
    VkDevice device = ctx.device;
    VkQueue queue = static_cast<VkQueue>(ctx.device.graphics);

    // With calibrated timestamps (Device asked for them) the two clocks are tied again at every collect,
    // without them once here : they drift apart over a long capture
    auto& dispatch = VULKAN_HPP_DEFAULT_DISPATCHER;
    bool calibrated = ctx.device.calibrated_timestamps && dispatch.vkGetPhysicalDeviceCalibrateableTimeDomainsEXT && dispatch.vkGetCalibratedTimestampsEXT;
    if(calibrated) {
        tracy = TracyVkContextCalibrated(physical, device, queue, static_cast<VkCommandBuffer>(command),
            dispatch.vkGetPhysicalDeviceCalibrateableTimeDomainsEXT, dispatch.vkGetCalibratedTimestampsEXT);
    } else {
        tracy = TracyVkContext(physical, device, queue, static_cast<VkCommandBuffer>(command));
    }
    const char name[] = "Vulkan";
    TracyVkContextName(tracy, name, sizeof(name) - 1);

    ctx.device->destroy(pool);

    dy::log(dy::info) << "Profiler : zones on the GPU" << (calibrated ? ", calibrated timestamps" : "") << "\n";

#endif

}

void GpuProfile::collect(vk::CommandBuffer command) {

#ifdef TRACY_ENABLE
    if(!tracy || collected.exchange(true)) return;
    TracyVkCollect(tracy, static_cast<VkCommandBuffer>(command));
#endif

}

GpuProfile::~GpuProfile() {

#ifdef TRACY_ENABLE
    if(tracy) TracyVkDestroy(tracy);
#endif

}
