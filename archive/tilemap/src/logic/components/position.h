#ifndef POSITION_H
#define POSITION_H

#include <glm/glm.hpp>

namespace dy {

class Position : public glm::vec2 {
public:
    // Move with MapManager.move ONLY
    Position(glm::vec2 pos, float height) {
        this->x = pos.x;
        this->y = pos.y;
        this->height = height;
    }
    glm::vec3 getVec3() const {
        return glm::vec3(x, y, height);
    }
    float getHeight() const {
        return height;
    }
private:
    float height;
};

//typedef const Position_ Position;

}

#endif
