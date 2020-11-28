#ifndef MODEL_MANAGER_H
#define MODEL_MANAGER_H

#include "modelc.h"

#include <unordered_map>
#include "entt/entt.hpp"

class GLTFLoader;

class ModelManager {
public:
	ModelManager(entt::registry& reg);
	void load(std::string name);
	entt::entity get(std::string name);
	~ModelManager();
private:
	entt::registry& reg;
	std::unique_ptr<GLTFLoader> gltf_loader;
	std::unordered_map<std::string, entt::entity> map;
};

#endif