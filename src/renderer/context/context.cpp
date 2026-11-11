#include "context.h"

#include "renderer/lighting/lights.h"

using namespace dy;

Context::Context(entt::registry& reg) : vr(*this, reg), win(*this, reg), instance(*this, reg), device(*this, reg), transfer(*this, reg), compute(*this, reg), swap(*this, reg) {

    //OPTICK_GPU_INIT_VULKAN(reinterpret_cast<VkDevice*> (&device.logical), reinterpret_cast<VkPhysicalDevice*> (&device.physical), reinterpret_cast<VkQueue*> (&device.graphics), &device.g_i, 1, nullptr);

    reg.ctx().emplace<Context*>(this);

    // Needs the device : session, reference space and per-eye swapchains
    vr.init();

    auto bindings = std::vector{
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1,
                                       vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1,
                                       vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eUniformBuffer, 1,
                                       vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
    };
    view_layout = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));

}

std::vector<vk::DescriptorPoolSize> Context::viewPoolSizes(uint32_t sets) const {
    return {
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, sets * 2),
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, sets * 2),
    };
}

void Context::writeViewSet(vk::DescriptorSet set, uint32_t frame, bool first) {

    // The map of the shade is replaced when plants come and go
    auto shade = vk::DescriptorImageInfo(tree_shade_sampler, tree_shade_view, vk::ImageLayout::eShaderReadOnlyOptimal);
    std::vector<vk::WriteDescriptorSet> writes {
        vk::WriteDescriptorSet(set, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &shade, nullptr, nullptr)
    };

    // The soil maps are one image for good (SoilMaps)
    auto soil = vk::DescriptorImageInfo(soil_sampler, soil_view, vk::ImageLayout::eShaderReadOnlyOptimal);
    auto lights = vk::DescriptorBufferInfo(lights_buffers[frame], 0, sizeof(LightsUBO));
    if(first) {
        writes.push_back(vk::WriteDescriptorSet(set, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &soil, nullptr, nullptr));
        writes.push_back(vk::WriteDescriptorSet(set, 3, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &lights, nullptr));
    }

    device->updateDescriptorSets(writes, {});

}

Context::~Context() {

    device->destroy(view_layout);

    vr.finish();

}
