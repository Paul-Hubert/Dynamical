#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include "entt/entt.hpp"
#include <vector>

#include <nlohmann/json.hpp>

#define TINYGLTF_NO_INCLUDE_JSON
#define TINYGLTF_USE_CPP14
#include "tiny_gltf.h"


#include "logic/components/model/modelc.h"

class GLTFLoader {
public:
	GLTFLoader(entt::registry& reg);
	entt::entity load(std::string path);
	~GLTFLoader();
private:
	entt::registry& reg;
	tinygltf::TinyGLTF loader;
	void makePart(tinygltf::Model& m, int index, ModelC::Part::View& v, std::vector<entt::entity>& buffers, int type);

};

#endif