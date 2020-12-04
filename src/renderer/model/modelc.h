#ifndef MODELC_H
#define MODELC_H

#include "entt/entt.hpp"
#include "glm/glm.hpp"

#include "bufferc.h"
#include "imagec.h"
#include "materialc.h"

#include "BulletCollision/Gimpact/btGImpactShape.h"
#include <BulletCollision/CollisionShapes/btTriangleIndexVertexArray.h>

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

		std::vector<int> index_data;
		std::vector<btScalar> vertex_data;

		std::shared_ptr<MaterialC> material;
	};

	std::vector<Part> parts;

	// Material
	std::shared_ptr<ImageC> color_image;


	btScalar mass;
	btVector3 local_inertia;

	std::unique_ptr<btTriangleIndexVertexArray> tivma = nullptr;

	std::unique_ptr<btCollisionShape> shape = nullptr;

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