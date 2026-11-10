#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "logic/grass/grass_bake.h"

using namespace dy;

namespace {

const float pi = 3.14159265359f;

// Small bake : 13 x 13 x 8 rays would not tell much, 48 x 48 x 8 runs in a few ms
GrassBakeParams smallBake() {
    GrassBakeParams params;
    params.size = 48;
    params.angles = 8;
    return params;
}

struct Random {
    uint32_t seed = 12345;
    float next() {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) * (1.f / 16777216.f);
    }
};

float texelHeight(const uint8_t* texel) {
    return texel[0] / 255.f;
}

}

TEST_SUITE("unit") {

TEST_CASE("GRASS.1 the grass tile is the same for a seed and stays under the start of the baked rays") {

    GrassModel a = generateGrassTile(), b = generateGrassTile();
    REQUIRE(a.blades > 100);
    REQUIRE(a.vertices.size() == b.vertices.size());
    REQUIRE(a.indices.size() % 3 == 0);
    REQUIRE(a.normals.size() == a.vertices.size());

    float top = 0.f;
    for(size_t i = 0; i < a.vertices.size(); i++) {
        CHECK(a.vertices[i] == b.vertices[i]);
        CHECK(a.vertices[i].y >= 0.f);
        CHECK(glm::length(a.normals[i]) == doctest::Approx(1.f).epsilon(1e-3));
        top = std::max(top, a.vertices[i].y);
    }
    CHECK(top == doctest::Approx(a.max_height));
    CHECK(top < 1.f);

    GrassModelParams other;
    other.seed = 8;
    CHECK(generateGrassTile(other).vertices[0] != a.vertices[0]);

}

TEST_CASE("GRASS.2 the tile repeats to infinity under the rays, and the ground stops them") {

    GrassTracer tracer(generateGrassTile());
    Random random;

    int blades = 0;
    for(int i = 0; i < 2000; i++) {
        float azimuth = random.next() * 2.f * pi, elevation = (10.f + 80.f * random.next()) * pi / 180.f;
        glm::vec3 direction(std::cos(azimuth) * std::cos(elevation), -std::sin(elevation), std::sin(azimuth) * std::cos(elevation));
        glm::vec3 origin(random.next(), 1.f, random.next());

        GrassTracer::Hit here, there;
        REQUIRE(tracer.trace(origin, direction, here));
        REQUIRE(tracer.trace(origin + glm::vec3(7.f, 0.f, -12.f), direction, there));
        CHECK(here.blade == there.blade);
        CHECK(here.t == doctest::Approx(there.t).epsilon(1e-3));

        // Never under the ground, and the normal faces the ray
        CHECK(origin.y + direction.y * here.t >= -1e-4f);
        CHECK(glm::dot(here.normal, direction) <= 1e-4f);
        blades += here.blade;
    }
    // Dense enough that most rays stop on a blade
    CHECK(blades > 1000);

    // Rays that never come down : nothing over the grass
    GrassTracer::Hit hit;
    CHECK_FALSE(tracer.trace(glm::vec3(0.5f, 1.2f, 0.5f), glm::vec3(1.f, 0.f, 0.f), hit));

}

TEST_CASE("GRASS.3 the lookup is the traced hit at 45 degrees and from the top") {

    GrassBakeParams params = smallBake();
    GrassRaycast data = bakeGrass(params, 2);
    GrassTracer tracer(generateGrassTile(params.model));

    REQUIRE(data.size == params.size);
    REQUIRE(data.angles == params.angles);
    REQUIRE(data.oblique.size() == size_t(params.angles) * params.size * params.size * 4);
    REQUIRE(data.top.size() == size_t(params.size) * params.size * 4);
    CHECK(data.key == grassBakeKey(params));

    Random random;
    for(int i = 0; i < 500; i++) {
        int x = int(random.next() * params.size), z = int(random.next() * params.size), a = int(random.next() * params.angles);
        glm::vec2 center((x + 0.5f) / params.size, (z + 0.5f) / params.size);
        float azimuth = (a + 0.5f) / params.angles * 2.f * pi;
        glm::vec2 along(std::cos(azimuth), std::sin(azimuth));

        // A ray at 45 degrees crosses one tile and is over the texel half way down
        glm::vec2 entry = center - 0.5f * along;
        GrassTracer::Hit hit;
        REQUIRE(tracer.trace(glm::vec3(entry.x, 1.f, entry.y), glm::vec3(along.x, -1.f, along.y), hit));
        float traced = 1.f - hit.t;

        const uint8_t* texel = &data.oblique[((size_t(a) * params.size + z) * params.size + x) * 4];
        CHECK(std::abs(texelHeight(texel) - traced) < 0.003f);
        glm::vec3 normal = decodeGrassNormal(glm::vec2(texel[1], texel[2]) / 255.f);
        CHECK(glm::dot(normal, hit.normal) > 0.99f);
        CHECK(std::abs(texel[3] / 255.f - hit.id) < 0.003f);

        GrassLookup lookup = lookupGrass(data, entry, along, 1.f, 0.f);
        CHECK(std::abs(lookup.height - traced) < 0.003f);
        CHECK(std::abs(lookup.id - hit.id) < 0.003f);
        CHECK(glm::dot(lookup.normal, hit.normal) > 0.99f);
        CHECK(std::abs(lookup.baked - traced) < 0.003f);

        // Straight down on the texel
        REQUIRE(tracer.trace(glm::vec3(center.x, 1.f, center.y), glm::vec3(0.f, -1.f, 0.f), hit));
        CHECK(std::abs(sampleGrassTop(data, center).r - (1.f - hit.t)) < 0.003f);
        CHECK(std::abs(lookupGrass(data, center, along, 0.f, 0.f).height - (1.f - hit.t)) < 0.03f);
    }

}

TEST_CASE("GRASS.7 normals survive their two values, a blade has one id and the ground 0.5") {

    Random random;
    for(int i = 0; i < 2000; i++) {
        glm::vec3 normal = glm::normalize(glm::vec3(random.next(), random.next(), random.next()) * 2.f - 1.f);
        glm::vec2 encoded = encodeGrassNormal(normal);
        CHECK(encoded.x >= 0.f);
        CHECK(encoded.x <= 1.f);
        CHECK(encoded.y >= 0.f);
        CHECK(encoded.y <= 1.f);
        CHECK(glm::dot(decodeGrassNormal(encoded), normal) > 0.9999f);
        // 8 bits each
        CHECK(glm::dot(decodeGrassNormal(glm::round(encoded * 255.f) / 255.f), normal) > 0.999f);
    }
    CHECK(encodeGrassNormal(glm::vec3(0.f, 1.f, 0.f)) == glm::vec2(0.5f));

    GrassModel model = generateGrassTile();
    REQUIRE(model.ids.size() == model.vertices.size());
    int differing = 0;
    for(size_t t = 0; t + 2 < model.indices.size(); t += 3) {
        float id = model.ids[model.indices[t]];
        CHECK(id >= 0.f);
        CHECK(id < 1.f);
        CHECK(model.ids[model.indices[t + 1]] == id);
        CHECK(model.ids[model.indices[t + 2]] == id);
        if(t >= 3) differing += model.ids[model.indices[t - 3]] != id;
    }
    CHECK(differing == int(model.blades) - 1);

    // Straight down between the tufts : the ground
    GrassBakeParams params = smallBake();
    GrassRaycast data = bakeGrass(params, 2);
    GrassTracer tracer(model);
    int ground = 0;
    for(int z = 0; z < params.size; z++) {
        for(int x = 0; x < params.size; x++) {
            GrassTracer::Hit hit;
            REQUIRE(tracer.trace(glm::vec3((x + 0.5f) / params.size, 1.f, (z + 0.5f) / params.size), glm::vec3(0.f, -1.f, 0.f), hit));
            if(!hit.blade) {
                ground++;
                CHECK(hit.id == 0.5f);
                CHECK(data.top[(size_t(z) * params.size + x) * 4 + 3] == 128);
            }
        }
    }
    CHECK(ground > 0);

}

TEST_CASE("GRASS.4 rays looked up from the middle of the grass are closer to the traced ones than from its top") {

    // No texture here : the "baked" 45 degree rays are traced on the spot, through the middle of what the
    // view ray crosses or through its entry point at the top
    GrassTracer tracer(generateGrassTile());
    Random random;

    for(float degrees : {25.f, 35.f, 55.f, 65.f}) {
        float elevation = degrees * pi / 180.f;
        float reach = std::cos(elevation) / std::sin(elevation);
        double middle_error = 0., top_error = 0.;
        const int count = 4000;

        for(int i = 0; i < count; i++) {
            float azimuth = random.next() * 2.f * pi;
            glm::vec2 along(std::cos(azimuth), std::sin(azimuth));
            glm::vec2 entry(random.next(), random.next());

            GrassTracer::Hit hit;
            tracer.trace(glm::vec3(entry.x, 1.f, entry.y), glm::vec3(along.x * reach, -1.f, along.y * reach), hit);
            float truth = 1.f - hit.t;

            // Baked ray through the same point of the middle : the hit is (0.5 - height) tiles after it
            glm::vec2 middle = entry + along * (0.5f * reach) - along * 0.5f;
            tracer.trace(glm::vec3(middle.x, 1.f, middle.y), glm::vec3(along.x, -1.f, along.y), hit);
            float from_middle = std::clamp(0.5f - (0.5f - (1.f - hit.t)) / reach, 0.f, 1.f);

            // Baked ray through the same point of the top : the hit is hit.t tiles after it
            tracer.trace(glm::vec3(entry.x, 1.f, entry.y), glm::vec3(along.x, -1.f, along.y), hit);
            float from_top = std::clamp(1.f - hit.t / reach, 0.f, 1.f);

            middle_error += std::abs(from_middle - truth);
            top_error += std::abs(from_top - truth);
        }

        INFO("elevation " << degrees << " : middle " << middle_error / count << ", top " << top_error / count);
        CHECK(middle_error < top_error);
    }

}

TEST_CASE("GRASS.5 baked rays come back from their file, a file baked with other parameters is stale") {

    GrassBakeParams params = smallBake();
    GrassRaycast data = bakeGrass(params, 2);

    auto path = (std::filesystem::temp_directory_path() / "dynamical_grass_test.bin").string();
    REQUIRE(saveGrassRaycast(data, path));

    GrassRaycast read;
    REQUIRE(loadGrassRaycast(read, path));
    CHECK(read.size == data.size);
    CHECK(read.angles == data.angles);
    CHECK(read.key == data.key);
    CHECK(read.oblique == data.oblique);
    CHECK(read.top == data.top);

    GrassBakeParams other = params;
    other.model.seed++;
    CHECK(grassBakeKey(other) != read.key);
    other = params;
    other.angles *= 2;
    CHECK(grassBakeKey(other) != read.key);

    // Truncated or foreign files are refused
    {
        std::ofstream os(path, std::ios::binary | std::ios::trunc);
        os << "not grass";
    }
    CHECK_FALSE(loadGrassRaycast(read, path));
    std::filesystem::remove(path);
    CHECK_FALSE(loadGrassRaycast(read, path));

}

TEST_CASE("GRASS.6 mip chains halve every dimension and average the texels") {

    // 4 x 2 x 2 : texel value = x
    std::vector<uint8_t> image;
    for(int z = 0; z < 2; z++) {
        for(int y = 0; y < 2; y++) {
            for(int x = 0; x < 4; x++) {
                for(int c = 0; c < 4; c++) image.push_back(uint8_t(x * 10 + c));
            }
        }
    }

    int levels = 8;
    std::vector<uint8_t> chain = rgbaMipChain(image, 4, 2, 2, levels);
    CHECK(levels == 3);                                 // 4x2x2, 2x1x1, 1x1x1
    REQUIRE(chain.size() == (16 + 2 + 1) * 4);
    CHECK(std::equal(image.begin(), image.end(), chain.begin()));
    CHECK(chain[16 * 4] == 5);                          // mean of x = 0 and 1
    CHECK(chain[17 * 4] == 25);                         // mean of x = 2 and 3
    CHECK(chain[18 * 4 + 1] == 16);                     // mean of 6 and 26

    levels = 1;
    CHECK(rgbaMipChain(image, 4, 2, 2, levels).size() == image.size());
    CHECK(levels == 1);

}

}
