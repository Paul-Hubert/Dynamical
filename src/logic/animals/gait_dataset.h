#ifndef DY_ANIMAL_GAIT_DATASET_H
#define DY_ANIMAL_GAIT_DATASET_H

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "logic/animals/gait.h"

namespace dy {

// What the walks of a set of demonstrations are drawn from : each value uniformly between its two ends, per walk.
// Lengths (clearance, soft_depth, crouch, spread) are in heights of the animal, so one file suits a cat and a horse.
struct GaitRanges {
    GaitStyle low, high;
    float speed[2] = { 0.6f, 1.4f };        // times the speed of the clip; redrawn every few seconds of a walk
    float radius[2] = { 1.5f, 8.f };        // of the turns, in lengths of the animal
    float straight = 0.3f;                  // part of the time spent going straight
    float soft = 0.5f;                      // part of the walks on soft ground (the others have a depth of 0)
    float scale[2] = { 0.9f, 1.1f };        // size of the animal
    float segment[2] = { 2.f, 5.f };        // seconds before the speed and the turn are drawn again

    // The engine's choice : every parameter of GaitStyle over the range it makes sense in
    static GaitRanges defaults();
    // { "stride": [0.8, 1.25], "soft_depth": [0, 0.15], ..., "speed": [0.6, 1.4], "radius": [1.5, 8], "straight": 0.3,
    //   "soft": 0.5, "scale": [0.9, 1.1], "segment": [2, 5] } : what is not given keeps `base`'s
    static GaitRanges parse(const nlohmann::json& j, GaitRanges base = defaults());
    nlohmann::json json() const;
};

struct GaitDemoOptions {
    std::string species;                    // id in the library
    std::string gait = "walk";              // walk, trot or run : the clip the walks are made from
    int samples = 64;                       // walks
    float seconds = 10.f;                   // of each
    float warmup = 2.f;                     // seconds walked before a walk is recorded
    int fps = 30;
    uint32_t seed = 1;
    bool terrain = true;                    // on the marching cubes terrain, started where it is gentle; false : a level plane
    int terrain_seed = 1337;
    GaitRanges ranges = GaitRanges::defaults();
};

struct GaitDemoResult {
    bool ok = false;
    std::string error;
    std::filesystem::path header, data;
    int samples = 0, frames = 0, joints = 0, legs = 0;
    size_t sample_floats = 0, frame_floats = 0;
    float planted = 0.f;                    // part of the foot frames that were on the ground
    float mean_speed = 0.f;                 // m/s
    float slide = 0.f;                      // m/s : how fast planted feet moved, 99 frames in 100
};

// Walks an animal of the library with styles, speeds and turns drawn from `options.ranges`, and writes what it did :
// <directory>/<species>_<gait>.json (what is in the file : joints, legs, the names of the values) and
// <directory>/<species>_<gait>.bin (float32 : per walk its parameters, then its frames). docs/animals.md has the layout.
// Demonstrations for imitation learning : a walk is a point of the style space, the set covers the ranges.
GaitDemoResult writeGaitDemos(AnimalLibrary& library, const GaitDemoOptions& options, const std::filesystem::path& directory);

}

#endif
