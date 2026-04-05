#include "factory_list.h"

#include "logic/components/position.h"
#include "logic/components/renderable.h"
#include "logic/components/human.h"
#include "logic/components/basic_needs.h"
#include "logic/components/building.h"
#include "ai/aic.h"

#include "logic/map/map_manager.h"
#include <logic/components/storage.h>
#include <logic/components/item.h>
#include <logic/components/edible.h>
#include <ai/identity/entity_identity.h>
#include <ai/memory/ai_memory.h>

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
    reg.emplace<EntityIdentity>(entity);  // ready=false; LLM generates name/personality before first decision
    reg.emplace<AIMemory>(entity);

    return entity;
}

entt::entity dy::buildBuilding(entt::registry& reg, Building::Type type, glm::ivec2 origin,
                               entt::entity owner, const std::vector<glm::ivec2>& tiles, glm::ivec2 door_tile) {
    auto entity = reg.create();

    // Calculate center position and average height
    glm::vec2 origin_world(origin);
    const auto& tmpl = get_building_templates()[type];
    glm::vec2 footprint_center = origin_world + glm::vec2(tmpl.footprint) * 0.5f;

    auto& map = reg.ctx<MapManager>();
    float avg_height = 0.0f;
    int count = 0;
    for (const auto& tile_pos : tiles) {
        if (auto* tile = map.getTile(glm::vec2(tile_pos))) {
            avg_height += tile->level;
            count++;
        }
    }
    if (count > 0) avg_height /= count;

    // Add Building component
    auto& building = reg.emplace<Building>(entity);
    building.type = type;
    building.owner = owner;
    building.residents.push_back(owner);
    building.occupied_tiles = tiles;
    building.door_tile = door_tile;
    building.construction_progress = 1.0f;
    building.complete = true;

    // Add Position and Renderable
    reg.emplace<Position>(entity, footprint_center, avg_height);
    auto& renderable = reg.emplace<Renderable>(entity);
    renderable.color = Color("8B6914");  // Brown wood color

    // Insert into map
    map.insert(entity, footprint_center);

    // Mark tiles as occupied
    for (const auto& tile_pos : tiles) {
        auto* tile = map.getTile(glm::vec2(tile_pos));
        if (tile) {
            tile->building = entity;
            if (tile_pos == door_tile) {
                tile->building_role = Tile::door_tile;
            } else {
                tile->building_role = Tile::floor_tile;
            }
        }
    }

    return entity;
}
