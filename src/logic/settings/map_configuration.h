#ifndef DYNAMICAL_MAP_CONFIGURATION_H
#define DYNAMICAL_MAP_CONFIGURATION_H

#include <vector>
#include <imgui/imgui.h>
#include <stdint.h>
#include <cereal/types/vector.hpp>

namespace dy {

class MapConfiguration {
public:
    MapConfiguration();

    std::vector<double> points_x;
    std::vector<double> points_y;
    uint32_t octave_count;
    float frequency;
    uint32_t seed;
    float gain;
    float weighted_strength;
    float lacunarity;
    float amplitude;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(
                CEREAL_NVP(points_x),
                CEREAL_NVP(points_y),
                CEREAL_NVP(octave_count),
                CEREAL_NVP(frequency),
                CEREAL_NVP(seed),
                CEREAL_NVP(gain),
                CEREAL_NVP(weighted_strength),
                CEREAL_NVP(lacunarity),
                CEREAL_NVP(amplitude)
        );
    }
};

}
#endif //DYNAMICAL_MAP_CONFIGURATION_H
