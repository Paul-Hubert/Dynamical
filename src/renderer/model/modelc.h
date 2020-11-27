#ifndef MODELC_H
#define MODELC_H

#include "entt/entt.hpp"
#include "glm/glm.hpp"

#include "bufferc.h"
#include "imagec.h"
#include "materialc.h"

class ModelC {
public:
	bool ready = false;

	struct Part {
		struct View {
			std::shared_ptr<BufferC> buffer;
			size_t size;
			size_t offset;
		};
		View index;
		View position;
		View normal;
		View uv;

		std::shared_ptr<MaterialC> material;
	};

	std::vector<Part> parts;

	// Material
	std::shared_ptr<ImageC> color_image;

	// Checks if ready, if not check if everything is uploaded and set to ready
	bool isReady(entt::registry& reg, entt::entity model) {
		if(ready) {
			return true;
		}
		for(auto& part : parts) {
			if(
				(part.index.buffer && !part.index.buffer->ready) ||
				(part.position.buffer && !part.position.buffer->ready) ||
				(part.normal.buffer && !part.normal.buffer->ready) ||
				(part.uv.buffer && !part.uv.buffer->ready)
				) {
				return false;
			}
		}
		ready = true;
		return true;
	}

};

#endif