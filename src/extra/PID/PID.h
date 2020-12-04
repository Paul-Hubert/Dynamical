#ifndef PID_H
#define PID_H

class PID {
public:
	PID(float p, float i, float d, float dt, float lower_limit, float upper_limit);

	float tick(float input, float target);

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
};
#endif
