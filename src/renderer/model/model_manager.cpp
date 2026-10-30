#include "model_manager.h"

#include "gltf_loader.h"
#include "model.h"

#include "util/log.h"

#include <exception>

using namespace dy;

ModelManager::ModelManager(entt::registry& reg) : reg(reg) {

}

entt::entity ModelManager::load(const std::string& path) {

    auto it = models.find(path);
    if(it != models.end()) {
        return it->second;
    }

    try {
        entt::entity entity = reg.create();
        reg.emplace<ModelC>(entity, loadGLTF(reg, path));
        models.emplace(path, entity);
        return entity;
    } catch(const std::exception& e) {
        dy::log(dy::error) << "Could not load model " << path << " : " << e.what() << "\n";
        return entt::null;
    }

}

entt::entity ModelManager::get(const std::string& path) const {
    auto it = models.find(path);
    return it != models.end() ? it->second : entt::null;
}
