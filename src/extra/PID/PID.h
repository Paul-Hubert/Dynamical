#ifndef PID_H
#define PID_H

class PID {
public:
	PID(float p, float i, float d, float dt, float lower_limit, float upper_limit);

	float tick(float input, float target);

	void setWrapped(float lower_limit, float upper_limit) {
		this->wrapped = true;
		wrap_lower_limit = lower_limit;
		wrap_upper_limit = upper_limit;
	}

	float p;
	float i;
	float d;
	float dt;
	float lower_limit;
	float upper_limit;

private:
	bool first = true;
	float error_sum = 0;
	float last_error;
	bool wrapped = false;
	float wrap_lower_limit;
	float wrap_upper_limit;
};
#endif
