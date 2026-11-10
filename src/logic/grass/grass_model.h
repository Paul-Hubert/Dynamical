#ifndef DY_GRASS_MODEL_H
#define DY_GRASS_MODEL_H

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace dy {

// A tile of grass blades : 1 x 1 on the ground (roots with x and z in [0, 1)), y up, the tallest tip at
// max_height. Leaning blades may leave the tile, which is meant to be repeated in a grid (GrassTracer).
struct GrassModel {
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> normals;     // smooth, one per vertex
    std::vector<float> ids;             // one per vertex : a random value in 0..1, the same over a blade
    std::vector<uint32_t> indices;      // triangles
    uint32_t blades = 0;
    float max_height = 0.f;
};

struct GrassModelParams {
    uint32_t seed = 7;
    int tufts = 9;                  // tufts x tufts on a jittered grid
    int blades_min = 4;             // blades of a tuft
    int blades_max = 7;
    float height_min = 0.45f;       // of a tuft, before the tallest tip is scaled to max_height
    float height_max = 1.f;
    float max_height = 0.98f;       // under 1 : the baked rays start at y = 1
    float width = 0.024f;           // of a blade at the root
    float lean = 0.3f;              // how far a tip goes sideways, in blade heights
    float fold = 0.35f;             // depth of the V of a blade, in half widths
    int segments = 5;               // along a blade
};

// Deterministic for a seed
GrassModel generateGrassTile(const GrassModelParams& params = {});

// Wavefront OBJ of the tile, to look at the model in another tool
bool writeGrassObj(const GrassModel& model, const std::string& path);

}

#endif
