#ifndef PATHC_H
#define PATHC_H

#include <glm/glm.hpp>
#include <vector>

class PathC {
public:
    PathC(std::vector<glm::vec2> tiles) : tiles(tiles) {}
    std::vector<glm::vec2> tiles;
};

#endif
