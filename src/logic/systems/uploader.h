#ifndef UPLOADER_H
#define UPLOADER_H

#include "system.h"

#include "renderer/vk.h"

#include "entt/entt.hpp"
#include "renderer/num_frames.h"
#include <array>

class UploaderSys : public System {
public: 
	UploaderSys(entt::registry& reg) : System(reg) {};
	void preinit() override;
	void init() override;
	void tick() override;
	void finish() override;
	const char* name() override { return "UploaderSys"; };
private:
	vk::CommandPool pool;

	struct Upload {
		vk::CommandBuffer command;
		vk::Fence fence;

		std::vector<entt::entity> uploaded{};
	};

	std::vector<Upload> uploads{};

};

#endif