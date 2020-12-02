#ifndef VRINPUTC_H
#define VRINPUTC_H

#include "renderer/util/xr.h"
#include "glm/glm.hpp"
#include "glm/gtx/quaternion.hpp"

class VRInputC {
public:
	bool rendering = false;
	XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
	XrTime predicted_time;
	struct View {
		XrPosef pose;
	};
	View views[2];
	struct Hand {
		bool active = false;
		glm::vec3 position;
		glm::quat rotation;
		glm::vec3 linearVelocity;
		glm::vec3 angularVelocity;
		bool action = false;

	};
	Hand hands[2];
};

#endif
