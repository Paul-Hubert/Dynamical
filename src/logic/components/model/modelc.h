#ifndef MODELC_H
#define MODELC_H

#include "entt/entt.hpp"
#include "glm/glm.hpp"

#include "bufferuploadc.h"
#include "imageuploadc.h"

class ModelC {
public:

	bool ready = false;

	struct Part {
		struct View {
			entt::entity buffer = entt::null;
			uint32_t size;
			uint32_t offset;
		};
		View index;
		View position;
		View normal;
		View uv;
	};

	std::vector<Part> parts;

	// Material
	entt::entity color_image = entt::null;

	// Checks if ready, if not check if everything is uploaded and set to ready
	bool isReady(entt::registry& reg, entt::entity model) {
		if(ready) {
			return true;
		}
		for(auto& part : parts) {
			if(
				(part.index.buffer != entt::null && reg.has<BufferUploadC>(part.index.buffer)) ||
				(part.position.buffer != entt::null && reg.has<BufferUploadC>(part.position.buffer)) ||
				(part.normal.buffer != entt::null && reg.has<BufferUploadC>(part.normal.buffer)) ||
				(part.uv.buffer != entt::null && reg.has<BufferUploadC>(part.uv.buffer))
				) {
				return false;
			}
		}
		ready = true;
		return true;
	}
};

#endif