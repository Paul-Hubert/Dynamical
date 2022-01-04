#ifndef SELECTION_H
#define SELECTION_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "util/color.h"

namespace dy {
    
class Selection {
public:
    entt::entity entity = entt::null;
    Color color;
};

}

#endif
