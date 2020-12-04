#ifndef VR_HANDC_H
#define VR_HANDC_H

#include <glm/glm.hpp>
#include <btBulletDynamicsCommon.h>
#include "extra/PID/PID.h"

class VRHandC {
public:
	const static int left = 0;
	const static int right = 1;
	int index;
	bool active = false;
	std::vector<PID> position_pids;
	std::vector<PID> rotation_pids;
};

#endif