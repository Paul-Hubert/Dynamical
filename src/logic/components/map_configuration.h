#ifndef DYNAMICAL_MAP_CONFIGURATION_H
#define DYNAMICAL_MAP_CONFIGURATION_H

#include <vector>
#include <imgui/imgui.h>
#include <stdint.h>

class MapConfiguration {
public:
    std::vector<float> points_x;
    std::vector<float> points_y;
    uint32_t octave_count;
    float frequency;
    uint32_t seed;
    float gain;
    float weighted_strength;
    float lacunarity;


};
#endif //DYNAMICAL_MAP_CONFIGURATION_H
