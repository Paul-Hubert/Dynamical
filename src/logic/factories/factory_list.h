#ifndef FACTORY_LIST_H
#define FACTORY_LIST_H

#include "entt/entt.hpp"

#define DEFINE_FACTORY(NAME, ...) \
namespace NAME {\
    entt::entity build(entt::registry& reg, __VA_ARGS__);\
}

DEFINE_FACTORY(ObjectFactory, entt::entity model)

#undef DEFINE_FACTORY
#endif