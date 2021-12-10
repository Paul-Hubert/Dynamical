#include "factory_list.h"

#include "logic/components/position.h"
#include "logic/components/renderable.h"
#include "logic/components/human.h"
#include "logic/components/basic_needs.h"
#include "ai/aic.h"

#include "logic/map/map_manager.h"
#include <logic/components/storage.h>
#include <logic/components/item.h>
#include <logic/components/edible.h>

using namespace dy;

void dy::destroy(entt::registry& reg, entt::entity entity) {
    reg.destroy(entity);
}

void dy::destroyObject(entt::registry& reg, entt::entity object) {
    reg.ctx<MapManager>().remove(object);
    dy::destroy(reg, object);
}

entt::entity dy::buildObject(entt::registry& reg, glm::vec2 position, Object::Type type, Object::Identifier id, glm::vec2 size, Color color) {
    auto entity = reg.create();
    reg.emplace<Object>(entity, type, id);
    auto& renderable = reg.emplace<Renderable>(entity);
    renderable.size = size;
    renderable.color = color;
    auto& map = reg.ctx<MapManager>();
    map.insert(entity, position);
    return entity;
}

entt::entity dy::buildPlant(entt::registry& reg, glm::vec2 position, Object::Identifier id, glm::vec2 size, Color color) {
    auto entity = dy::buildObject(reg, position, Object::plant, id, size, color);
    return entity;
}

entt::entity dy::buildTree(entt::registry& reg, glm::vec2 position) {
    return dy::buildPlant(reg, position, Object::tree, glm::vec2(1,1), Color(0.03,0.47,0.19,1.0));
}

entt::entity dy::buildBerryBush(entt::registry& reg, glm::vec2 position) {
    auto entity = dy::buildPlant(reg, position, Object::berry_bush, glm::vec2(1,1), Color(0.60,0.0588,0.2941,1.0));
    return entity;
}

entt::entity dy::buildAnimal(entt::registry& reg, glm::vec2 position, Object::Type type, Object::Identifier id, Color color) {
    entt::entity entity = dy::buildObject(reg, position, type, id, glm::vec2(1,1), color);
    auto& needs = reg.emplace<BasicNeeds>(entity);
    needs.hunger = 10;
    reg.emplace<AIC>(entity);
    reg.emplace<Storage>(entity);
    return entity;
}

entt::entity dy::buildBunny(entt::registry& reg, glm::vec2 position) {
    entt::entity entity = dy::buildAnimal(reg, position, Object::being, Object::bunny, Color(0.8431,0.8627,0.8667,1.0));
    return entity;
}

entt::entity dy::buildHuman(entt::registry& reg, glm::vec2 position, Color color) {
    entt::entity entity = dy::buildAnimal(reg, position, Object::being, Object::human, color);
    reg.emplace<Human>(entity);
    return entity;
}
