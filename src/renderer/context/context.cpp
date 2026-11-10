#include "context.h"

using namespace dy;

Context::Context(entt::registry& reg) : vr(*this, reg), win(*this, reg), instance(*this, reg), device(*this, reg), transfer(*this, reg), compute(*this, reg), swap(*this, reg) {

    //OPTICK_GPU_INIT_VULKAN(reinterpret_cast<VkDevice*> (&device.logical), reinterpret_cast<VkPhysicalDevice*> (&device.physical), reinterpret_cast<VkQueue*> (&device.graphics), &device.g_i, 1, nullptr);

    reg.ctx().emplace<Context*>(this);

    // Needs the device : session, reference space and per-eye swapchains
    vr.init();

    auto bindings = std::vector{
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1,
                                       vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
    };
    view_layout = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));

}

Context::~Context() {

    device->destroy(view_layout);

    vr.finish();

}
