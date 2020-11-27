#ifndef MATERIAL_MANAGER_H
#define MATERIAL_MANAGER_H

#include "entt/entt.hpp"
#include "renderer/vk.h"

class Context;

class MaterialManager {
public:
	MaterialManager(entt::registry& reg, Context& ctx);
	void update();
	~MaterialManager();

	vk::DescriptorSetLayout layout;

private:
	entt::registry& reg;
	Context& ctx;
};

#endif