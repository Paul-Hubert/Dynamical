#include "spawn.h"

#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "view/components/model_renderable.h"

#include "renderer/model/model_manager.h"

using namespace dy;

static const char* box_path = "./resources/models/box.gltf";

entt::entity dy::spawnBox(entt::registry& reg, glm::vec3 position, float size, glm::vec4 color, glm::vec3 velocity) {

    entt::entity model = reg.ctx().get<ModelManager>().load(box_path);
    if(model == entt::null) return entt::null;

    // The box model is a unit cube centered on its origin
    auto entity = reg.create();
    reg.emplace<Transform>(entity, position, glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(size));
    reg.emplace<ModelRenderable>(entity, model, color);
    auto& body = reg.emplace<RigidBody>(entity);
    body.half_extent = glm::vec3(size * 0.5f);
    body.initial_velocity = velocity;
    return entity;

}
