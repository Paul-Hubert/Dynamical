#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>
#include "position.h"

namespace dy {
    
class Camera {
public:
    // center
    glm::vec2 center;
    glm::vec2 corner;
    glm::vec2 size;
    
    glm::vec2 fromWorldSpace(glm::vec2 position, glm::vec2 screen_size) {
        return (position - corner) / size * screen_size;
    }
    
    glm::vec2 fromScreenSpace(glm::vec2 position, glm::vec2 screen_size) {
        return corner + size * position / screen_size;
    }
    
    glm::vec2 fromWorldSize(glm::vec2 world_size, glm::vec2 screen_size) {
        return world_size * screen_size / size;
    }
    
    glm::vec2 fromScreenSize(glm::vec2 pixel_size, glm::vec2 screen_size) {
        return pixel_size / screen_size * size;
    }
    
};

}

#endif
