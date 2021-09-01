#ifndef POSITION_H
#define POSITION_H

#include <glm/glm.hpp>

namespace dy {
    
class Position : public glm::vec2 {
public:
    Position(glm::vec2 pos) {
        x = pos.x;
        y = pos.y;
    }
};

}

#endif
