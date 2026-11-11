#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "logic/grass/grass_bake.h"
#include "logic/grass/grass_height.h"
#include "logic/terrain/ground_curve.h"

using namespace dy;

namespace {

const float pi = 3.14159265359f;

// Small bake : 13 x 13 x 8 rays would not tell much, 48 x 48 x 8 runs in a few ms.
// Another count of azimuths at each elevation
GrassBakeParams smallBake() {
    GrassBakeParams params;
    params.size = 48;
    params.angles = {12, 10, 8, 6};
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

TEST_CASE("GRASS.3 the lookup is the traced hit at each baked elevation and from the top") {

    GrassBakeParams params = smallBake();
    GrassRaycast data = bakeGrass(params, 2);
    GrassTracer tracer(generateGrassTile(params.model));

    REQUIRE(data.size == params.size);
    REQUIRE(data.angles == params.angles);
    REQUIRE(data.slices() == 12 + 10 + 8 + 6);
    REQUIRE(data.firstSlice(2) == 12 + 10);
    REQUIRE(data.oblique.size() == size_t(data.slices()) * params.size * params.size * 4);
    REQUIRE(data.top.size() == size_t(params.size) * params.size * 4);
    CHECK(data.key == grassBakeKey(params));

    // 22.5 degrees apart. The lowest rays cross grass_max_reach tiles from the top to half the grass
    CHECK(data.lowElevation() == doctest::Approx(std::atan(0.5f / grass_max_reach)));
    CHECK(data.height(0, 1.f) == 1.f);
    CHECK(data.height(0, 0.f) == 0.5f);
    CHECK(data.height(2, 0.3f) == 0.3f);
    CHECK(grassElevation(2) == doctest::Approx(pi / 4.f));
    CHECK(grassReach(0) == grass_max_reach);
    CHECK(grassReach(1) == doctest::Approx(1.f + std::sqrt(2.f)));
    CHECK(grassReach(2) == doctest::Approx(1.f));
    CHECK(grassReach(3) == doctest::Approx(std::sqrt(2.f) - 1.f));

    Random random;
    for(int i = 0; i < 800; i++) {
        int elevation = i % grass_elevations, angles = params.angles[elevation];
        int x = int(random.next() * params.size), z = int(random.next() * params.size), a = int(random.next() * angles);
        glm::vec2 center((x + 0.5f) / params.size, (z + 0.5f) / params.size);
        float azimuth = (a + 0.5f) / angles * 2.f * pi;
        glm::vec2 along(std::cos(azimuth), std::sin(azimuth));
        float reach = grassReach(elevation);

        // The ray crosses `reach` tiles and is over the texel half way : from the top of the grass to the
        // ground, from low_from to low_to for the lowest rays (r is 0 for what they meet further down)
        glm::vec2 entry = center - 0.5f * reach * along;
        float fall = elevation == 0 ? params.low_from - params.low_to : 1.f;
        GrassTracer::Hit hit;
        REQUIRE(tracer.trace(glm::vec3(entry.x, elevation == 0 ? params.low_from : 1.f, entry.y), glm::vec3(along.x * reach, -fall, along.y * reach), hit));
        float traced = std::max(1.f - hit.t, 0.f);

        const uint8_t* texel = &data.oblique[((size_t(data.firstSlice(elevation) + a) * params.size + z) * params.size + x) * 4];
        CHECK(std::abs(texelHeight(texel) - traced) < 0.003f);
        glm::vec3 normal = decodeGrassNormal(glm::vec2(texel[1], texel[2]) / 255.f);
        CHECK(glm::dot(normal, hit.normal) > 0.99f);
        CHECK(std::abs(texel[3] / 255.f - hit.id) < 0.003f);
        CHECK(std::abs(sampleGrassOblique(data, elevation, center, azimuth).r - traced) < 0.003f);

        // A view ray of that elevation from the same entry point. The lowest rays answer for the rays
        // along the ground, looked up as if they went down in grass_max_reach tiles : the hit is as far
        GrassLookup lookup = lookupGrass(data, entry, along, elevation == 0 ? 1e6f : reach, 0.f);
        CHECK(std::abs(lookup.height - traced) < 0.003f);
        CHECK(std::abs(lookup.id - hit.id) < 0.003f);
        CHECK(glm::dot(lookup.normal, hit.normal) > 0.99f);
        CHECK(std::abs(lookup.baked - data.height(elevation, traced)) < 0.003f);

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
    CHECK(read.oblique_colour == data.oblique_colour);
    CHECK(read.top_colour == data.top_colour);

    GrassBakeParams other = params;
    other.model.seed++;
    CHECK(grassBakeKey(other) != read.key);
    for(int e = 0; e < grass_elevations; e++) {
        other = params;
        other.angles[e] *= 2;
        CHECK(grassBakeKey(other) != read.key);
    }
    other = params;
    other.low_to += 0.1f;
    CHECK(grassBakeKey(other) != read.key);
    CHECK(read.low_from == data.low_from);
    CHECK(read.low_to == data.low_to);

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

    // Layers of a 2D array (the azimuths of the oblique rays) are kept apart : 4x2, 2x1, 1x1, twice.
    // The second layer is 100 brighter
    for(size_t i = 8 * 4; i < image.size(); i++) image[i] += 100;
    levels = 8;
    chain = rgbaMipChain(image, 4, 2, 2, levels, true);
    CHECK(levels == 3);
    REQUIRE(chain.size() == (16 + 4 + 2) * 4);
    CHECK(chain[16 * 4] == 5);
    CHECK(chain[17 * 4] == 25);
    CHECK(chain[18 * 4] == 105);                        // second layer
    CHECK(chain[19 * 4] == 125);
    CHECK(chain[20 * 4] == 15);
    CHECK(chain[21 * 4] == 115);

}

TEST_CASE("GRASS.9 with the lowest rays and those at 22.5 and 67.5 degrees the lookup is closer to the traced rays than from 45 degrees alone") {

    GrassBakeParams params;
    params.size = 128;
    params.angles = {48, 32, 32, 16};
    GrassRaycast data = bakeGrass(params);
    GrassTracer tracer(generateGrassTile(params.model));
    Random random;

    for(float degrees : {6.f, 12.f, 64.f, 70.f}) {
        float true_reach = 1.f / std::tan(degrees * pi / 180.f);
        float reach = std::min(true_reach, grass_max_reach);
        double several = 0., alone = 0.;
        const int count = 4000;

        for(int i = 0; i < count; i++) {
            float azimuth = random.next() * 2.f * pi;
            glm::vec2 along(std::cos(azimuth), std::sin(azimuth));
            glm::vec2 entry(random.next(), random.next());

            // Where the blade is met on the ground, as a height on the ray that is looked up : rays along
            // the ground are looked up as if they went down in grass_max_reach tiles
            GrassTracer::Hit hit;
            tracer.trace(glm::vec3(entry.x, 1.f, entry.y), glm::vec3(along.x * true_reach, -1.f, along.y * true_reach), hit);
            float truth = std::max(1.f - hit.t * true_reach / reach, 0.f);

            several += std::abs(lookupGrass(data, entry, along, true_reach, 0.f).height - truth);

            // The 45 degree rays through the middle, carried to the view ray (and to the top over them)
            glm::vec2 middle = entry + along * (0.5f * reach);
            float carried = std::clamp(0.5f - (0.5f - sampleGrassOblique(data, 2, middle, azimuth).r) / reach, 0.f, 1.f);
            if(reach < 1.f) {
                carried = glm::mix(carried, sampleGrassTop(data, middle).r, glm::smoothstep(0.f, 1.f, (degrees - 45.f) / 45.f));
            }
            alone += std::abs(carried - truth);
        }

        INFO("elevation " << degrees << " : " << several / count << ", from 45 degrees alone " << alone / count);
        CHECK(several < 0.9 * alone);
    }

}

TEST_CASE("GRASS.10 the ground bends as its densities say, and the grass is seen through where it falls away under the ray") {

    // A hill that is part of a sphere, sampled every 4 m like the terrain : the density grows inside
    const float radius = 60.f, spacing = 4.f;
    const glm::vec3 center(1.3f, -40.f, -2.1f);
    auto hill = [&](glm::ivec3 p) {
        return radius - glm::length(glm::vec3(p) * spacing - center);
    };

    // Over the top : the same curve every way, 1 / radius of the sphere through the sample
    glm::ivec3 top(0, 5, 0);
    float r = glm::length(glm::vec3(top) * spacing - center);
    glm::vec3 curve = groundCurve(hill, top, spacing);
    CHECK(curve.x == doctest::Approx(-1.f / r).epsilon(0.05));
    CHECK(curve.z == doctest::Approx(-1.f / r).epsilon(0.05));
    CHECK(std::abs(curve.y) < 0.002f);

    // On its side the ground bends more along the slope than across it : h = sqrt(r² - d²) at d from the axis
    glm::ivec3 side(8, 2, 0);
    glm::vec3 from = glm::vec3(side) * spacing - center;
    r = glm::length(from);
    float d = from.x, h = from.y;
    REQUIRE(std::abs(from.z) < 3.f);
    curve = groundCurve(hill, side, spacing);
    CHECK(curve.x == doctest::Approx(-r * r / (h * h * h)).epsilon(0.08));
    CHECK(curve.z == doctest::Approx(-1.f / h).epsilon(0.08));
    CHECK(d > 25.f);

    // A hollow bends the other way, a plane does not bend, a wall is no ground
    auto hollow = [&](glm::ivec3 p) {
        return glm::length(glm::vec3(p) * spacing - glm::vec3(0.f, 80.f, 0.f)) - radius;
    };
    CHECK(groundCurve(hollow, glm::ivec3(0, 5, 0), spacing).x == doctest::Approx(1.f / 60.f).epsilon(0.05));
    auto plane = [&](glm::ivec3 p) {
        return 3.f - 0.5f * p.x - 2.f * p.y + 0.25f * p.z;
    };
    CHECK(glm::length(groundCurve(plane, glm::ivec3(2, 1, -3), spacing)) < 1e-4f);
    auto wall = [&](glm::ivec3 p) {
        return 1.f - p.x * p.x - 0.01f * p.y;
    };
    CHECK(groundCurve(wall, glm::ivec3(1, 0, 0), spacing) == glm::vec3(0.f));

    // The ray over ground that falls away : sink l - falling (flat l)² / 2 under the top of the grass
    const float sink = 0.08f, falling = 1.f / 50.f, flat_length = 0.99f;
    float deepest = 0.f;
    for(float l = 0.f; l < 20.f; l += 0.01f) {
        deepest = std::max(deepest, sink * l - 0.5f * falling * flat_length * flat_length * l * l);
    }
    // What is under the deepest point of the ray is not there
    CHECK(grassSeeThrough(sink, falling, flat_length) == doctest::Approx(deepest).epsilon(0.01));
    // Nothing is cut on flat ground or in a hollow, everything where the ray only touches the grass
    CHECK(std::isinf(grassSeeThrough(sink, 0.f, flat_length)));
    CHECK(std::isinf(grassSeeThrough(sink, -0.02f, flat_length)));
    CHECK(grassSeeThrough(0.f, falling, flat_length) == 0.f);
    // Less and less towards the crest, and less on a sharper one
    CHECK(grassSeeThrough(0.04f, falling, flat_length) == doctest::Approx(0.25f * grassSeeThrough(0.08f, falling, flat_length)));
    CHECK(grassSeeThrough(0.08f, 2.f * falling, flat_length) == doctest::Approx(0.5f * grassSeeThrough(0.08f, falling, flat_length)));
    // Grass 1 m high on a crest of 50 m of radius : seen through from 11 degrees of the tangent
    CHECK(grassSeeThrough(0.25f, falling, flat_length) > 1.f);
    CHECK(grassSeeThrough(0.1f, falling, flat_length) < 0.3f);

}

TEST_CASE("GRASS.8 the height map of a chunk is the noise of the world on its vertices, the same on shared edges") {

    const int size = grass_height::size;
    REQUIRE(size == chunk::cubes + 1);

    GrassHeightField field;
    std::vector<uint8_t> map = field.bake(glm::ivec2(2, -3));
    REQUIRE(map.size() == grass_height::num_texels);
    CHECK(map == GrassHeightField().bake(glm::ivec2(2, -3)));

    // Texel (i, j) is the field at the vertex i, j cubes from the origin of the chunk
    const glm::vec2 origin = glm::vec2(2, -3) * (float) chunk::length;
    float worst = 0.f;
    for(int j = 0; j < size; j += 7) {
        for(int i = 0; i < size; i += 5) {
            float expected = field.sample(origin.x + (float) (i * chunk::cube_size), origin.y + (float) (j * chunk::cube_size));
            worst = std::max(worst, std::abs(map[(size_t) j * size + i] / 255.f - expected));
        }
    }
    CHECK(worst < 0.01f);

    // Neighbours bake the same heights on the edge they share : no step in the shell between chunks
    std::vector<uint8_t> east = field.bake(glm::ivec2(3, -3)), south = field.bake(glm::ivec2(2, -2));
    for(int k = 0; k < size; k++) {
        CHECK(map[(size_t) k * size + (size - 1)] == east[(size_t) k * size]);
        CHECK(map[(size_t) (size - 1) * size + k] == south[k]);
    }

    // As the shader reads it : the texels on the vertices, linear between them, clamped at the edges
    CHECK(sampleGrassHeightMap(map.data(), glm::vec2(0.f)) == doctest::Approx(map[0] / 255.f));
    CHECK(sampleGrassHeightMap(map.data(), glm::vec2((float) chunk::length)) == doctest::Approx(map.back() / 255.f));
    CHECK(sampleGrassHeightMap(map.data(), glm::vec2(12.f, 8.f)) == doctest::Approx(map[(size_t) 2 * size + 3] / 255.f));
    CHECK(sampleGrassHeightMap(map.data(), glm::vec2(14.f, 8.f)) == doctest::Approx(0.5f * (map[(size_t) 2 * size + 3] + map[(size_t) 2 * size + 4]) / 255.f));

    // Low frequency : short and tall grass both exist over a few chunks, and a terrain triangle (4 m)
    // never crosses much of the range
    int low = 0, high = 0, total = 0;
    float steepest = 0.f;
    double sum = 0.;
    for(int cz = 0; cz < 4; cz++) {
        for(int cx = 0; cx < 4; cx++) {
            std::vector<uint8_t> texels = field.bake(glm::ivec2(cx, cz));
            for(int j = 0; j < size; j++) {
                for(int i = 0; i < size; i++) {
                    float value = texels[(size_t) j * size + i] / 255.f;
                    low += value < 0.2f;
                    high += value > 0.8f;
                    sum += value;
                    total++;
                    if(i > 0) steepest = std::max(steepest, std::abs(value - texels[(size_t) j * size + i - 1] / 255.f));
                }
            }
        }
    }
    MESSAGE("height map : mean " << sum / total << ", under 0.2 " << (float) low / total << ", over 0.8 " << (float) high / total << ", steepest texel step " << steepest);

    // Baked on the main thread when a chunk gets its mesh (GrassPass::update) : how long that is
    auto start = std::chrono::steady_clock::now();
    uint8_t scratch[grass_height::num_texels];
    for(int k = 0; k < 200; k++) field.bake(glm::ivec2(k, -k), scratch);
    MESSAGE("height map : " << std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 200. << " us per chunk");
    CHECK(sum / total == doctest::Approx(0.5).epsilon(0.2));
    CHECK(low > total / 50);
    CHECK(high > total / 50);
    CHECK(steepest < 0.35f);

    // Octaves finer than two texels are left out, other parameters give another map and another key
    GrassHeightParams fine;
    fine.wavelength = 20.f;
    fine.octaves = 6;
    CHECK(grass_height::octaves(fine) == 2);    // 20 m and 10 m
    CHECK(grass_height::octaves(GrassHeightParams {}) == 4);
    CHECK(grassHeightKey(fine) != grassHeightKey(GrassHeightParams {}));
    CHECK(GrassHeightField(fine).bake(glm::ivec2(2, -3)) != map);

    GrassHeightParams seeded;
    seeded.seed = 8;
    CHECK(grassHeightKey(seeded) != grassHeightKey(GrassHeightParams {}));
    CHECK(GrassHeightField(seeded).bake(glm::ivec2(2, -3)) != map);

}

TEST_CASE("GRASS.11 every kind of plant makes a tile the rays can be baked on, with a key of its own") {

    for(int kind = 0; kind < grass_kinds; kind++) {
        GrassModelParams params;
        params.kind = static_cast<GrassKind>(kind);
        CAPTURE(grassKindInfo(params.kind).name);

        GrassModel a = generateGrassTile(params), b = generateGrassTile(params);
        REQUIRE(a.blades > 100);
        REQUIRE(a.indices.size() % 3 == 0);
        CHECK(a.indices.size() / 3 < 50000);
        REQUIRE(a.normals.size() == a.vertices.size());
        REQUIRE(a.ids.size() == a.vertices.size());
        REQUIRE(a.parts.size() == a.vertices.size());
        REQUIRE(a.vertices == b.vertices);
        CHECK(a.tile_size == grassKindInfo(params.kind).tile_size);
        CHECK(a.height > 0.1f);

        float top = 0.f;
        bool sound = true;
        for(size_t i = 0; i < a.vertices.size(); i++) {
            glm::vec3 v = a.vertices[i];
            // Roots are in the tile, leaves do not go further than the next one
            sound = sound && v.y >= 0.f && v.x > -1.f && v.x < 2.f && v.z > -1.f && v.z < 2.f
                && std::abs(glm::length(a.normals[i]) - 1.f) < 1e-3f && a.parts[i] <= uint8_t(GrassPart::Seed);
            top = std::max(top, v.y);
        }
        CHECK(sound);
        CHECK(top == doctest::Approx(a.max_height));
        for(uint32_t index : a.indices) REQUIRE(index < a.vertices.size());

        GrassKind named;
        REQUIRE(grassKindFromName(grassKindInfo(params.kind).name, named));
        CHECK(named == params.kind);

        // A ray from the top meets a plant or the ground
        GrassTracer tracer(std::move(a));
        int plants = 0;
        for(int i = 0; i < 400; i++) {
            GrassTracer::Hit hit;
            REQUIRE(tracer.trace(glm::vec3((i % 20 + 0.5f) / 20.f, 1.f, (i / 20 + 0.5f) / 20.f), glm::vec3(0.3f, -1.f, 0.2f), hit));
            plants += hit.blade;
        }
        CHECK(plants > 20);

        GrassBakeParams bake, meadow;
        bake.model.kind = params.kind;
        CHECK((grassBakeKey(bake) == grassBakeKey(meadow)) == (params.kind == GrassKind::Meadow));
        REQUIRE(tracer.model().colours.size() == tracer.model().vertices.size());
    }

}

}
