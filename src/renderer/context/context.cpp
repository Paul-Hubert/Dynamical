#include "context.h"

#include "renderer/lighting/lights.h"

using namespace dy;

Context::Context(entt::registry& reg) : vr(*this, reg), win(*this, reg), instance(*this, reg), device(*this, reg), transfer(*this, reg), compute(*this, reg), swap(*this, reg) {

    //OPTICK_GPU_INIT_VULKAN(reinterpret_cast<VkDevice*> (&device.logical), reinterpret_cast<VkPhysicalDevice*> (&device.physical), reinterpret_cast<VkQueue*> (&device.graphics), &device.g_i, 1, nullptr);

    reg.ctx().emplace<Context*>(this);

    // Needs the device : session, reference space and per-eye swapchains
    vr.init();

    // The compute shaders of the lighting read the same set
    auto stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;
    auto bindings = std::vector{
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, stages),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, stages),
        vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, stages),
        vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eUniformBuffer, 1, stages)
    };
    if(device.ray.enabled()) {
        bindings.push_back(vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eAccelerationStructureKHR, 1, stages));
        bindings.push_back(vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eStorageBuffer, 1, stages));
        bindings.push_back(vk::DescriptorSetLayoutBinding(6, vk::DescriptorType::eCombinedImageSampler, max_ray_textures, stages));
    }
    // The textures of the rays come as the species load : most of that array is not there
    std::vector<vk::DescriptorBindingFlags> flags(bindings.size());
    if(device.ray.enabled()) flags[6] = vk::DescriptorBindingFlagBits::ePartiallyBound;
    vk::DescriptorSetLayoutBindingFlagsCreateInfo binding_flags(flags);
    vk::DescriptorSetLayoutCreateInfo layout_info({}, bindings);
    if(device.ray.enabled()) layout_info.pNext = &binding_flags;
    view_layout = device->createDescriptorSetLayout(layout_info);

}

std::vector<vk::DescriptorPoolSize> Context::viewPoolSizes(uint32_t sets) const {
    std::vector<vk::DescriptorPoolSize> sizes {
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, sets * 2),
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, sets * 2),
    };
    if(device.ray.enabled()) {
        sizes.push_back(vk::DescriptorPoolSize(vk::DescriptorType::eAccelerationStructureKHR, sets));
        sizes.push_back(vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, sets));
        sizes[1].descriptorCount += sets * max_ray_textures;
    }
    return sizes;
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

    // What the rays see : the structure is built again every frame, and replaced when it grows
    vk::WriteDescriptorSetAccelerationStructureKHR scene(1, &tlas);
    if(device.ray.enabled() && tlas) {
        vk::WriteDescriptorSet write(set, 4, 0, 1, vk::DescriptorType::eAccelerationStructureKHR);
        write.pNext = &scene;
        writes.push_back(write);
    }
    auto geometries = vk::DescriptorBufferInfo(ray_geometries, 0, VK_WHOLE_SIZE);
    if(device.ray.enabled() && ray_geometries) {
        writes.push_back(vk::WriteDescriptorSet(set, 5, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &geometries, nullptr));
    }
    if(device.ray.enabled() && ray_textures) {
        size_t& written = ray_textures_written[(VkDescriptorSet) set];
        size_t count = std::min<size_t>(ray_textures->size(), max_ray_textures);
        if(written < count) {
            writes.push_back(vk::WriteDescriptorSet(set, 6, (uint32_t) written, (uint32_t) (count - written),
                vk::DescriptorType::eCombinedImageSampler, ray_textures->data() + written, nullptr, nullptr));
            written = count;
        }
    }

    device->updateDescriptorSets(writes, {});

}

Context::~Context() {

    device->destroy(view_layout);

    vr.finish();

}
