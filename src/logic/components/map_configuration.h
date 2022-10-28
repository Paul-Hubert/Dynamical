#ifndef DYNAMICAL_MAP_CONFIGURATION_H
#define DYNAMICAL_MAP_CONFIGURATION_H

#include <vector>
#include <imgui/imgui.h>

class MapConfiguration {
public:
    std::vector<ImVec2> points;
    float octave_count;
    float frequency;
    uint32_t seed;
    float gain;
    float weighted_strength;
    float lacunarity;


};
#endif //DYNAMICAL_MAP_CONFIGURATION_H
