#ifndef CAMERAC_H
#define CAMERAC_H

#include <glm/glm.hpp>

class CameraC {
public:
	glm::mat4 projection;
	glm::mat4 view;
    glm::vec3 position;

    float pitch = 0;
    float roll = 0;
    float yaw = 0;
};

#endif