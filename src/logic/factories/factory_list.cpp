#include "factory_list.h"

#include "logic/components/positionc.h"
#include "logic/components/renderablec.h"
#include "logic/components/human.h"
#include "logic/components/basic_needs.h"
#include "logic/ai/aic.h"

#include "logic/map/map_manager.h"

entt::entity dy::buildObject(entt::registry& reg, glm::vec2 position, glm::vec2 size, glm::vec4 color) {
    auto entity = reg.create();
    auto& renderable = reg.emplace<RenderableC>(entity);
    renderable.size = size;
    renderable.color = color;
    auto& map = reg.ctx<MapManager>();
    map.insert(entity, position);
    return entity;
}

entt::entity dy::buildTree(entt::registry& reg, glm::vec2 position) {
    return dy::buildObject(reg, position, glm::vec2(1,1), glm::vec4(0.03,0.47,0.19,1.0));
}

entt::entity dy::buildAnimal(entt::registry& reg, glm::vec2 position, glm::vec4 color) {
    entt::entity object = dy::buildObject(reg, position, glm::vec2(1,1), color);
    reg.emplace<BasicNeeds>(object);
    reg.emplace<AIC>(object);
    return object;
}

entt::entity dy::buildHuman(entt::registry& reg, glm::vec2 position, glm::vec4 color) {
    entt::entity animal = dy::buildAnimal(reg, position, color);
    reg.emplace<Human>(animal);
    return animal;
}
