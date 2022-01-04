#ifndef RENDERABLE_H
#define RENDERABLE_H

#include <glm/glm.hpp>

#include "util/color.h"

namespace dy {
    
class Renderable {
public:
    glm::vec2 size;
    Color color;
};

}

#endif
