#ifndef MATERIALC_H
#define MATERIALC_H

#include <memory>

#include "renderer/util/vk.h"

class MaterialC {
public:
	MaterialC(std::shared_ptr<ImageC> color) : color(color) {}
	std::shared_ptr<ImageC> color;
	vk::DescriptorSet set;
	vk::Sampler sampler;
};

#endif