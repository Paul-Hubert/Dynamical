#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>

namespace dy {
    
class Camera {
public:
    // center
    glm::vec2 center;
    glm::vec2 corner;
    glm::vec2 size;
};

}

#endif
