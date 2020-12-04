#ifndef VRINPUTC_H
#define VRINPUTC_H

#include "renderer/util/xr.h"
#include "glm/glm.hpp"
#include "glm/gtx/quaternion.hpp"

class VRInputC {
public:
	bool rendering = false;
	XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
	XrTime time;
	XrTime predicted_time;
	XrTime predicted_period;
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

		glm::vec3 predictedPosition;
		glm::quat predictedRotation;
		glm::vec3 predictedLinearVelocity;
		glm::vec3 predictedAngularVelocity;

		float trigger = 0;
		float grip = 0;

	};
	Hand hands[2];
};

#endif
