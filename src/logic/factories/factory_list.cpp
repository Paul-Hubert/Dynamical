#include "factory_list.h"

#include "logic/components/positionc.h"
#include "logic/components/renderablec.h"
#include "logic/components/human.h"
#include "logic/components/basic_needs.h"
#include "logic/components/plant.h"
#include "logic/ai/aic.h"

#include "logic/map/map_manager.h"

entt::entity dy::buildObject(entt::registry& reg, glm::vec2 position, glm::vec2 size, Color color) {
    auto entity = reg.create();
    auto& renderable = reg.emplace<RenderableC>(entity);
    renderable.size = size;
    renderable.color = color;
    auto& map = reg.ctx<MapManager>();
    map.insert(entity, position);
    return entity;
}

entt::entity dy::buildPlant(entt::registry& reg, glm::vec2 position, glm::vec2 size, Color color, int type) {
    auto entity = dy::buildObject(reg, position, size, color);
    reg.emplace<Plant>(entity, (Plant::Type) type);
    return entity;
}

entt::entity dy::buildTree(entt::registry& reg, glm::vec2 position) {
    return dy::buildPlant(reg, position, glm::vec2(1,1), Color(0.03,0.47,0.19,1.0), Plant::tree);
}

entt::entity dy::buildBerryBush(entt::registry& reg, glm::vec2 position) {
    return dy::buildPlant(reg, position, glm::vec2(1,1), Color(0.60,0.0588,0.2941,1.0), Plant::berry_bush);
}

entt::entity dy::buildAnimal(entt::registry& reg, glm::vec2 position, Color color) {
    entt::entity object = dy::buildObject(reg, position, glm::vec2(1,1), color);
    reg.emplace<BasicNeeds>(object);
    reg.emplace<AIC>(object);
    return object;
}

entt::entity dy::buildBunny(entt::registry& reg, glm::vec2 position) {
    entt::entity animal = dy::buildAnimal(reg, position, Color(0.8431,0.8627,0.8667,1.0));
    return animal;
}

entt::entity dy::buildHuman(entt::registry& reg, glm::vec2 position, Color color) {
    entt::entity animal = dy::buildAnimal(reg, position, color);
    reg.emplace<Human>(animal);
    return animal;
}
