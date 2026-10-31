#ifndef DY_WORLD_STEPPER_H
#define DY_WORLD_STEPPER_H

#include "logic/systems/system.h"

#include <cstdint>

namespace dy {

// Host context variable: where the drawn worlds are between their last two ticks (WorldView), and the panel state
struct WorldStepperState {
    double alpha = 1.;
    double h = 1. / 60.;
    bool paused = false;
    bool step_once = false;
    uint64_t ticks = 0;
};

// Steps the scene's worlds in real time from the frame clock (dynamical): at most 8 ticks a frame, pause and
// single step from its panel. Under capture the frame clock is fixed, so captures stay reproducible
class WorldStepperSys : public System {
public:
    WorldStepperSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "WorldStepper"; }
    void preinit() override;
    void tick(double dt) override;

    static constexpr int max_ticks = 8;

private:
    double accumulator = 0.;
};

}

#endif
