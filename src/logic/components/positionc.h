#ifndef POSITIONC_H
#define POSITIONC_H

#include "glm/vec3.hpp"

class PositionC {
public:
	PositionC(glm::vec3 pos) : pos(pos) {}
	glm::vec3 pos;
};

#endif