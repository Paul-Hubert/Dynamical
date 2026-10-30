#ifndef RAYCAST_DATA_H
#define RAYCAST_DATA_H

#include <array>
#include "glm/glm.hpp"
#include "cereal/types/array.hpp"

// Lookup table of the grass shader, baked by tools/raycast into resources/raycast_data.bin.
// For a position in a grass tile and a 2D ray angle : distance to the first blade and its normal.

constexpr int num_angles = 32;
constexpr int num_samples = 128;

class Sample {
public:
    Sample() : pos(0) {}
    Sample(glm::u8vec4 pos) : pos(pos) {};
    glm::u8vec4 pos;

    template <class Archive>
    void serialize( Archive & ar ) {
        ar(pos.x, pos.y, pos.z, pos.w);
    }
};

class RaycastData {
public:

    template <class Archive>
    void serialize( Archive & ar ) {
        ar(data);
    }

    std::array<Sample, num_angles * num_samples * num_samples> data;

};

#endif
