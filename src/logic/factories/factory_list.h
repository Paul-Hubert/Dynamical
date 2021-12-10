#ifndef FACTORY_LIST_H
#define FACTORY_LIST_H

#include "entt/entt.hpp"

#include <glm/glm.hpp>

#include "util/color.h"

#include "logic/components/object.h"

#define DEFINE_FACTORY(NAME, ...) \
entt::entity build##NAME(entt::registry& reg, __VA_ARGS__);

namespace dy {

DEFINE_FACTORY(Object, glm::vec2 position, Object::Type type, Object::Identifier id, glm::vec2 size, Color color)

DEFINE_FACTORY(Plant, glm::vec2 position, Object::Identifier id, glm::vec2 size, Color color)

DEFINE_FACTORY(Tree, glm::vec2 position)

DEFINE_FACTORY(BerryBush, glm::vec2 position)

DEFINE_FACTORY(Animal, glm::vec2 position, Object::Type type, Object::Identifier id, Color color)

DEFINE_FACTORY(Bunny, glm::vec2 position)

DEFINE_FACTORY(Human, glm::vec2 position, Color color)

void destroy(entt::registry& reg, entt::entity entity);

void destroyObject(entt::registry& reg, entt::entity object);

}

#undef DEFINE_FACTORY
#endif
