#ifndef SELECTIONC_H
#define SELECTIONC_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "util/color.h"

class SelectionC {
public:
    entt::entity entity = entt::null;
    Color color;
};

#endif
