#ifndef VR_HANDC_H
#define VR_HANDC_H

#include <glm/glm.hpp>
#include <btBulletDynamicsCommon.h>

class VRHandC {
public:
	const static int left = 0;
	const static int right = 1;
	int index;
	bool active = false;
	glm::vec3 last_predicted_linear_velocity;
	glm::vec3 last_predicted_angular_velocity;
	glm::vec3 last_load;
	float last_dt;
};

#endif