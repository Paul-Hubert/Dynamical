#include "model_manager.h"

#include "gltf_loader.h"
#include "model.h"

#include "renderer/context/context.h"
#include "util/log.h"
#include "util/primitive_mesh.h"

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

entt::entity ModelManager::add(const std::string& key, const MeshData& mesh) {

    if(auto it = models.find(key); it != models.end()) return it->second;

    Context& ctx = *reg.ctx().get<Context*>();

    std::vector<ModelVertex> vertices(mesh.positions.size());
    ModelC model;
    model.min = glm::vec3(1e30f);
    model.max = glm::vec3(-1e30f);
    for(size_t i = 0; i < vertices.size(); i++) {
        vertices[i] = { mesh.positions[i], mesh.normals[i], i < mesh.uvs.size() ? mesh.uvs[i] : glm::vec2(0.f) };
        model.min = glm::min(model.min, mesh.positions[i]);
        model.max = glm::max(model.max, mesh.positions[i]);
    }

    model.vertices = ctx.transfer.createBuffer(vertices.data(), vk::BufferCreateInfo({}, vertices.size() * sizeof(ModelVertex),
        vk::BufferUsageFlagBits::eVertexBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));
    model.indices = ctx.transfer.createBuffer(mesh.indices.data(), vk::BufferCreateInfo({}, mesh.indices.size() * sizeof(uint32_t),
        vk::BufferUsageFlagBits::eIndexBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));
    model.materials.push_back(ModelC::Material());
    model.parts.push_back({ 0, (uint32_t) mesh.indices.size(), 0 });

    entt::entity entity = reg.create();
    reg.emplace<ModelC>(entity, std::move(model));
    models.emplace(key, entity);
    return entity;

}

entt::entity ModelManager::get(const std::string& path) const {
    auto it = models.find(path);
    return it != models.end() ? it->second : entt::null;
}
