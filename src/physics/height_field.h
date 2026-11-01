#ifndef DY_HEIGHT_FIELD_H
#define DY_HEIGHT_FIELD_H

#include <cstdint>
#include <vector>

#include "logic/components/rigid_body.h"

namespace dy {

// Draws the waves of a HeightField: wavelengths log-uniform in its range, directions and phases uniform,
// amplitudes wave_slope / k so that every wave has the same steepest slope
void drawWaves(HeightField& field, uint64_t seed);

// Row-major samples (z then x) of the field's surface, as Jolt's HeightFieldShapeSettings expects
std::vector<float> sampleHeights(const HeightField& field);

}

#endif
