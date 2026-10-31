#ifndef DY_FRAME_CLOCK_H
#define DY_FRAME_CLOCK_H

#include <cstdint>

namespace dy {

// Context variable written by the frame loop before the systems tick.
// With RunArgs::fixed_frame_dt, dt is fixed and time is frame * dt: nothing reads the wall clock.
struct FrameClock {
    double dt = 0.;
    double time = 0.;
    uint64_t frame = 0;     // 1 for the first frame
};

}

#endif
