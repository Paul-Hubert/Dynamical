#ifndef POSITIONC_H
#define POSITIONC_H

#include <glm/glm.hpp>

class PositionC : public glm::vec2 {
public:
    PositionC(glm::vec2 pos) {
        x = pos.x;
        y = pos.y;
    }
};

#endif
