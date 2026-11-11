#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "logic/terrain/ground_relief.h"

using namespace dy;

namespace {

// A round stone in the middle of a flat tile
std::vector<uint8_t> stone(int size) {
    std::vector<uint8_t> heights(size_t(size) * size, 20);
    for(int y = 0; y < size; y++) {
        for(int x = 0; x < size; x++) {
            float dx = (x + 0.5f) / size - 0.5f, dy = (y + 0.5f) / size - 0.5f;
            float r = std::sqrt(dx * dx + dy * dy) / 0.3f;
            if(r < 1.f) heights[size_t(y) * size + x] = uint8_t(20.f + 220.f * std::sqrt(1.f - r * r));
        }
    }
    return heights;
}

}

TEST_SUITE("unit") {

TEST_CASE("RELIEF.1 the height map of a ground texture is a tile of triangles, from its lowest texel to its highest") {

    std::vector<uint8_t> heights = stone(64);
    GrassModel model = groundReliefModel(heights.data(), 64, 64, 32);

    // A vertex per texel and the first ones again : the tile closes on itself
    REQUIRE(model.vertices.size() == 33 * 33);
    REQUIRE(model.indices.size() == 32 * 32 * 6);
    REQUIRE(model.normals.size() == model.vertices.size());
    REQUIRE(model.colours.size() == model.vertices.size());
    float low = 1.f, high = 0.f;
    for(auto& v : model.vertices) {
        low = std::min(low, v.y);
        high = std::max(high, v.y);
    }
    CHECK(low == doctest::Approx(0.f));
    CHECK(high == doctest::Approx(model.max_height));
    for(int z = 0; z < 33; z++) {
        CHECK(model.vertices[size_t(z) * 33].y == model.vertices[size_t(z) * 33 + 32].y);
        CHECK(model.vertices[size_t(z)].y == model.vertices[size_t(32) * 33 + z].y);
    }

    // The top of the stone is in the middle of the tile
    glm::vec3 top(0.f);
    for(auto& v : model.vertices) {
        if(v.y > top.y) top = v;
    }
    CHECK(std::abs(top.x - 0.5f) < 0.05f);
    CHECK(std::abs(top.z - 0.5f) < 0.05f);

}

TEST_CASE("RELIEF.2 the rays baked on the relief meet the stone where a ray traced on it does") {

    std::vector<uint8_t> heights = stone(64);
    GroundReliefParams params;
    params.size = 32;
    params.angles = {24, 16, 16, 8};
    GrassRaycast data = bakeGroundRelief(heights.data(), 64, 64, params, 2);
    REQUIRE(data.size == 32);
    REQUIRE(data.slices() == 64);
    CHECK(data.key == groundReliefKey(heights.data(), 64, 64, params));

    // From the top : the height map itself
    GrassModel model = groundReliefModel(heights.data(), 64, 64, 32);
    for(int z = 0; z < 32; z++) {
        for(int x = 0; x < 32; x++) {
            float baked = data.top[(size_t(z) * 32 + x) * 4] / 255.f;
            CHECK(std::abs(baked - model.vertices[size_t(z) * 33 + x].y) < 0.01f);
        }
    }

    // The lookup of the shader, from every side at 45 degrees and lower : the height of the traced hit,
    // nearly (two baked views are blended)
    GrassTracer tracer(model, 16);
    double error = 0., flat = 0.;
    int count = 0;
    for(int i = 0; i < 400; i++) {
        float azimuth = i * 2.39996f, reach = 1.f + (i % 3);
        glm::vec2 along(std::cos(azimuth), std::sin(azimuth)), entry((i % 20 + 0.5f) / 20.f, (i / 20 + 0.5f) / 20.f);
        GrassTracer::Hit hit;
        REQUIRE(tracer.trace(glm::vec3(entry.x, 1.f, entry.y), glm::vec3(along.x * reach, -1.f, along.y * reach), hit));
        float traced = 1.f - hit.t;
        error += std::abs(lookupGrass(data, entry, along, reach, 0.f).height - traced);
        flat += std::abs(0.3f - traced);
        count++;
    }
    INFO("mean error of the hit height " << error / count << ", " << flat / count << " for a flat tile");
    CHECK(error / count < 0.5 * flat / count);

    // Other heights, another key
    heights[100]++;
    CHECK(groundReliefKey(heights.data(), 64, 64, params) != data.key);

}

}
