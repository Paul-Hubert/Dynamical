#include "material_manager.h"

#include "renderer/context.h"


MaterialManager::MaterialManager(entt::registry& reg, Context& ctx) : reg(reg), ctx(ctx) {

	layout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, 
		vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, {})
		));

}

void MaterialManager::update() {

}

MaterialManager::~MaterialManager() {
	ctx.device->destroy(layout);
}