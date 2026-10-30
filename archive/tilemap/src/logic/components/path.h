#ifndef PATH_H
#define PATH_H

#include <glm/glm.hpp>
#include <vector>

namespace dy {
    
class Path {
public:
    Path(std::vector<glm::vec2> tiles) : tiles(tiles) {}
    std::vector<glm::vec2> tiles;
};

}

#endif
