#ifndef VRINPUTC_H
#define VRINPUTC_H

#include "renderer/util/xr.h"

class VRInputC {
public:
	bool rendering = false;
	XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
	XrTime predicted_time;
	struct Hand {
		bool active = false;
		XrPosef pose;
		bool action = false;
	};
	Hand per_hand[2];
};

#endif
