#include "PID.h"

#include "util/log.h"

PID::PID(float p, float i, float d, float dt, float lower_limit, float upper_limit) : p(p), i(i), d(d), dt(dt), lower_limit(lower_limit), upper_limit(upper_limit) {

}

float PID::tick(float input, float target) {
    
    float error = target - input;
    
    error_sum += error * dt;
    float derror = (error - last_error) / dt;

    float output = p * error + i * error_sum + d * derror;

    if (output > upper_limit) output = upper_limit;
    else if (output < lower_limit) output = lower_limit;
    
    last_error = error;
    if (first) {
        first = false;
        return 0.f;
    }
    return output;
}
