#include "map_configuration.h"

using namespace dy;

MapConfiguration::MapConfiguration() {
    frequency = 0.001;
    gain = 0.6f;
    lacunarity = 1.8f;
    octave_count = 6;
    seed = 12345;
    weighted_strength = 0.0f;
    amplitude = 10;

    points_x.emplace_back(-1);
    points_y.emplace_back(-10);

    points_x.emplace_back(1);
    points_y.emplace_back(10);
}