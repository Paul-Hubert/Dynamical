#ifndef FACTORY_LIST_H
#define FACTORY_LIST_H

#include "entt/entt.hpp"

#include <glm/glm.hpp>

#include "util/color.h"

#define DEFINE_FACTORY(NAME, ...) \
entt::entity build##NAME(entt::registry& reg, __VA_ARGS__);

namespace dy {

DEFINE_FACTORY(Object, glm::vec2 position, glm::vec2 size, Color color)

DEFINE_FACTORY(Plant, glm::vec2 position, glm::vec2 size, Color color, int type)

DEFINE_FACTORY(Tree, glm::vec2 position)

DEFINE_FACTORY(BerryBush, glm::vec2 position)

DEFINE_FACTORY(Animal, glm::vec2 position, Color color)

DEFINE_FACTORY(Bunny, glm::vec2 position)

DEFINE_FACTORY(Human, glm::vec2 position, Color color)

}

#undef DEFINE_FACTORY
#endif
