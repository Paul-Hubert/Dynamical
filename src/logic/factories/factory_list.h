#ifndef FACTORY_LIST_H
#define FACTORY_LIST_H

#include "entt/entt.hpp"

#include <glm/glm.hpp>

#define DEFINE_FACTORY(NAME, ...) \
entt::entity build##NAME(entt::registry& reg, __VA_ARGS__);

namespace dy {

DEFINE_FACTORY(Object, glm::vec2 position, glm::vec2 size, glm::vec4 color)

DEFINE_FACTORY(Tree, glm::vec2 position)

DEFINE_FACTORY(Animal, glm::vec2 position, glm::vec4 color)

DEFINE_FACTORY(Human, glm::vec2 position, glm::vec4 color)

}

#undef DEFINE_FACTORY
#endif
