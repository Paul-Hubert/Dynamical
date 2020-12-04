#include "PID.h"

#include "util/log.h"

PID::PID(float p, float i, float d, float dt, float lower_limit, float upper_limit) : p(p), i(i), d(d), dt(dt), lower_limit(lower_limit), upper_limit(upper_limit) {

}

float PID::tick(float input, float target) {

    float error;
    if (wrapped)
    {
        /*
         * There are three ways to traverse from one point to another in this setup.
         *
         *    1)  Target --> Feedback
         *
         * The other two ways involve bridging a gap connected by the upper and
         * lower bounds of the feedback wrap.
         *
         *    2)  Target --> Upper Bound == Lower Bound --> Feedback
         *
         *    3)  Target --> Lower Bound == Upper Bound --> Feedback
         *
         * Of these three paths, one should always be shorter than the other two,
         * unless all three are equal, in which case it does not matter which path
         * is taken.
         */
        float regErr = target - input;
        float altErr1 = (target - wrap_lower_limit) + (wrap_upper_limit - input);
        float altErr2 = (wrap_upper_limit - target) + (input - wrap_lower_limit);

        //Calculate the absolute values of each error.
        float regErrAbs = (regErr >= 0) ? regErr : -regErr;
        float altErr1Abs = (altErr1 >= 0) ? altErr1 : -altErr1;
        float altErr2Abs = (altErr2 >= 0) ? altErr2 : -altErr2;

        //Use the error with the smallest absolute value
        if (regErrAbs <= altErr1Abs && regErr <= altErr2Abs) //If reguErrAbs is smallest
        {
            error = regErr;
        }
        else if (altErr1Abs < regErrAbs && altErr1Abs < altErr2Abs) //If altErr1Abs is smallest
        {
            error = altErr1Abs;
        }
        else
        {
            error = altErr2Abs;
        }
    }
    else
    {
        //Calculate the error between the feedback and the target.
        error = target - input;
    }
    
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
