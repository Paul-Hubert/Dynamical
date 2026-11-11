#include "ground_relief.h"

#include <algorithm>
#include <cmath>

namespace dy {

namespace {

// Part of the key : to change with anything that changes the rays for the same heights
constexpr uint32_t relief_version = 1;

GrassBakeParams bakeParams(const GroundReliefParams& params) {
    GrassBakeParams bake;
    bake.size = std::max(params.size, 4);
    bake.angles = params.angles;
    return bake;
}

}

GrassModel groundReliefModel(const uint8_t* heights, int width, int height, int size, float max_height) {

    // Averaged down to the grid
    std::vector<float> grid(size_t(size) * size, 0.f);
    float low = 255.f, high = 0.f;
    for(int z = 0; z < size; z++) {
        for(int x = 0; x < size; x++) {
            int x0 = x * width / size, x1 = std::max((x + 1) * width / size, x0 + 1);
            int y0 = z * height / size, y1 = std::max((z + 1) * height / size, y0 + 1);
            float sum = 0.f;
            for(int j = y0; j < y1; j++) {
                for(int i = x0; i < x1; i++) sum += heights[size_t(j % height) * width + i % width];
            }
            float value = sum / float((x1 - x0) * (y1 - y0));
            grid[size_t(z) * size + x] = value;
            low = std::min(low, value);
            high = std::max(high, value);
        }
    }
    const float range = std::max(high - low, 1.f);

    // A vertex in the middle of each texel, and one more row and column : the first ones again, a tile further
    GrassModel model;
    const int n = size + 1;
    model.vertices.reserve(size_t(n) * n);
    for(int z = 0; z < n; z++) {
        for(int x = 0; x < n; x++) {
            float value = (grid[size_t(z % size) * size + x % size] - low) / range;
            model.vertices.push_back(glm::vec3((x + 0.5f) / size, value * max_height, (z + 0.5f) / size));
        }
    }
    model.indices.reserve(size_t(size) * size * 6);
    for(int z = 0; z < size; z++) {
        for(int x = 0; x < size; x++) {
            uint32_t a = uint32_t(z * n + x), b = a + 1, c = a + n, d = c + 1;
            for(uint32_t index : {a, c, b, b, c, d}) model.indices.push_back(index);
        }
    }

    model.normals.assign(model.vertices.size(), glm::vec3(0.f));
    for(size_t i = 0; i + 2 < model.indices.size(); i += 3) {
        uint32_t a = model.indices[i], b = model.indices[i + 1], c = model.indices[i + 2];
        glm::vec3 normal = glm::cross(model.vertices[b] - model.vertices[a], model.vertices[c] - model.vertices[a]);
        model.normals[a] += normal;
        model.normals[b] += normal;
        model.normals[c] += normal;
    }
    for(auto& normal : model.normals) {
        float length = glm::length(normal);
        normal = length > 0.f ? normal / length : glm::vec3(0.f, 1.f, 0.f);
    }

    model.ids.assign(model.vertices.size(), 0.5f);
    model.parts.assign(model.vertices.size(), static_cast<uint8_t>(GrassPart::Blade));
    model.colours.assign(model.vertices.size(), glm::vec3(0.5f));
    model.blades = 1;
    model.max_height = max_height;
    return model;

}

uint64_t groundReliefKey(const uint8_t* heights, int width, int height, const GroundReliefParams& params) {
    uint64_t hash = grassBakeKey(bakeParams(params));
    auto add = [&](uint32_t value) {
        for(int i = 0; i < 4; i++) {
            hash = (hash ^ ((value >> (8 * i)) & 0xff)) * 1099511628211ull;
        }
    };
    add(relief_version);
    add(uint32_t(width));
    add(uint32_t(height));
    for(size_t i = 0; i < size_t(width) * height; i++) hash = (hash ^ heights[i]) * 1099511628211ull;
    return hash;
}

GrassRaycast bakeGroundRelief(const uint8_t* heights, int width, int height, const GroundReliefParams& params, unsigned threads) {
    GrassBakeParams bake = bakeParams(params);
    // Two triangles per texel everywhere : a cell of the tracer for every two texels
    return bakeGrassModel(groundReliefModel(heights, width, height, bake.size), bake, groundReliefKey(heights, width, height, params), threads,
        std::max(bake.size / 2, 1));
}

}
