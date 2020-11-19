#ifndef MESHC_H
#define MESHC_H

#include "glm/glm.hpp"
#include <vector>

class MeshC {
public:
	struct Vertex {
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
	};

	std::vector<Vertex> vertices{};
	std::vector<uint16_t> indices{};

	entt::entity vertex_buffer;
	entt::entity index_buffer;

};

#endif