#include "model_manager.h"

#include "renderer/context/context.h"
#include "gltf_loader.h"

ModelManager::ModelManager(entt::registry& reg) : reg(reg), gltf_loader(std::make_unique<GLTFLoader>(reg)) {
	
}

void ModelManager::load(std::string name) {
	map.insert({name, gltf_loader->load(name)});
}

entt::entity ModelManager::get(std::string name) {
	auto it = map.find(name);
	if(it != map.end()) {
		return it->second;
	}
	return entt::null;
}

ModelManager::~ModelManager() {

}