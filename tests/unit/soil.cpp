#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "logic/terrain/soil.h"

using namespace dy;

namespace {

int at(const std::vector<uint8_t>& map, int i, int j, int channel) {
    return map[((size_t) j * soil::size + i) * soil::channels + channel];
}

TEST_CASE("SOIL.1 the soil map of a chunk is the field of the world on its vertices, the same where neighbours overlap") {

    const int size = soil::size, border = soil::border;
    REQUIRE(size == chunk::cubes + 1 + 2 * border);

    SoilField field;
    std::vector<uint8_t> map = field.bake(glm::ivec2(2, -3));
    REQUIRE(map.size() == soil::num_texels * soil::channels);
    CHECK(map == SoilField().bake(glm::ivec2(2, -3)));

    // Texel (i, j) is the field at the vertex i - border, j - border cubes from the origin of the chunk
    const glm::vec2 origin = glm::vec2(2, -3) * (float) chunk::length;
    float worst = 0.f;
    for(int j = 0; j < size; j += 6) {
        for(int i = 0; i < size; i += 6) {
            glm::vec4 expected = field.sample(origin.x + (float) ((i - border) * chunk::cube_size), origin.y + (float) ((j - border) * chunk::cube_size));
            for(int channel = 0; channel < soil::channels; channel++) {
                worst = std::max(worst, std::abs(at(map, i, j, channel) / 255.f - expected[channel]));
            }
        }
    }
    CHECK(worst < 0.02f);

    // Neighbours bake the same values where they overlap, a border to each side of the edge they share :
    // no step in a patch between chunks, wherever within the border the shaders read
    std::vector<uint8_t> east = field.bake(glm::ivec2(3, -3)), south = field.bake(glm::ivec2(2, -2));
    int different = 0;
    for(int k = 0; k < size; k++) {
        for(int o = 0; o <= 2 * border; o++) {
            for(int channel = 0; channel < soil::channels; channel++) {
                different += at(map, size - 1 - 2 * border + o, k, channel) != at(east, o, k, channel);
                different += at(map, k, size - 1 - 2 * border + o, channel) != at(south, k, o, channel);
            }
        }
    }
    CHECK(different == 0);

    // As the shaders read it : the texels on the vertices, linear between them, clamped at the border
    auto texel = [&](int i, int j) {
        return glm::vec4(at(map, i + border, j + border, 0), at(map, i + border, j + border, 1), at(map, i + border, j + border, 2), at(map, i + border, j + border, 3)) / 255.f;
    };
    for(int channel = 0; channel < soil::channels; channel++) {
        CHECK(sampleSoilMap(map.data(), glm::vec2(0.f))[channel] == doctest::Approx(texel(0, 0)[channel]));
        CHECK(sampleSoilMap(map.data(), glm::vec2((float) chunk::length))[channel] == doctest::Approx(texel(chunk::cubes, chunk::cubes)[channel]));
        CHECK(sampleSoilMap(map.data(), glm::vec2(12.f, 8.f))[channel] == doctest::Approx(texel(3, 2)[channel]));
        CHECK(sampleSoilMap(map.data(), glm::vec2(14.f, 8.f))[channel] == doctest::Approx(0.5f * (texel(3, 2)[channel] + texel(4, 2)[channel])));
        CHECK(sampleSoilMap(map.data(), glm::vec2(-4.f, -4.f))[channel] == doctest::Approx(texel(-1, -1)[channel]));
        CHECK(sampleSoilMap(map.data(), glm::vec2(-40.f, 8.f))[channel] == doctest::Approx(texel(-1, 2)[channel]));
    }

}

TEST_CASE("SOIL.2 each kind of soil covers the part of the ground it was given, in patches, the grass shorter around them") {

    const int size = soil::size, border = soil::border;
    SoilParams params;
    SoilField field(params);
    const float wanted[soil::kinds] = { params.mud.cover, params.stones.cover, params.sand.cover };

    // 64 chunks : 4 km²
    int inside[soil::kinds] = {}, edge[soil::kinds] = {}, any = 0, total = 0;
    int grass_deep = 0, deep = 0, short_grass = 0, steps = 0, steep_steps = 0, steep_grass = 0;
    for(int cz = -4; cz < 4; cz++) {
        for(int cx = -4; cx < 4; cx++) {
            std::vector<uint8_t> map = field.bake(glm::ivec2(cx, cz));
            for(int j = border; j < size - 1 - border; j++) {
                for(int i = border; i < size - 1 - border; i++) {
                    bool bare = false, well_inside = false;
                    for(int kind = 0; kind < soil::kinds; kind++) {
                        int value = at(map, i, j, kind);
                        inside[kind] += value > 127;
                        edge[kind] += value > 0 && value < 255;
                        bare |= value > 127;
                        well_inside |= value == 255;
                        steps++;
                        steep_steps += std::abs(value - at(map, i + 1, j, kind)) > 191;
                    }
                    int grass = at(map, i, j, 3);
                    deep += well_inside;
                    grass_deep += well_inside && grass > 64;
                    short_grass += grass < 255;
                    steep_grass += std::abs(grass - at(map, i + 1, j, 3)) > 191;
                    any += bare;
                    total++;
                }
            }
        }
    }

    for(int kind = 0; kind < soil::kinds; kind++) {
        float cover = (float) inside[kind] / total;
        MESSAGE("soil " << kind << " : cover " << cover << " for " << wanted[kind] << ", " << (float) edge[kind] / total << " of the texels on an edge");
        CHECK(cover == doctest::Approx(wanted[kind]).epsilon(0.35));
        // Patches and not speckles : the map is at 0 or 1 nearly everywhere, between them on the edges only
        CHECK(edge[kind] < total / 8);
    }
    MESSAGE("soil : " << (float) any / total << " of the ground is bare, " << (float) steep_steps / steps << " of the texel steps over 0.75");
    CHECK((float) any / total < wanted[0] + wanted[1] + wanted[2] + 0.05f);

    // An edge is edge_width wide, two texels : what the shaders read between texels is a slope, not a
    // step, nearly everywhere (the noise is not a distance where it is flat)
    CHECK(steep_steps < steps / 500);

    // No grass well inside the patches, all of it away from them, and no step on the way : the shell
    // of the grass reads this on the terrain vertices
    MESSAGE("soil : grass shorter over " << (float) short_grass / total << " of the ground, " << (float) steep_grass / total
        << " of its texel steps over 0.75, grass on " << (float) grass_deep / std::max(deep, 1) << " of the texels well inside a patch");
    CHECK(steep_grass < total / 500);
    CHECK(grass_deep < deep / 20);
    CHECK(short_grass > any);
    CHECK(short_grass < total / 2);

    // Baked on the main thread when a chunk gets its mesh (SoilMaps::update) : how long that is
    auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> scratch(soil::num_texels * soil::channels);
    for(int k = 0; k < 200; k++) field.bake(glm::ivec2(k, -k), scratch.data());
    MESSAGE("soil map : " << std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 200. << " us per chunk");

}

TEST_CASE("SOIL.3 parameters : cover, octaves, key, the grass next to the patches") {

    std::vector<uint8_t> map = SoilField().bake(glm::ivec2(2, -3));

    // No cover, no patch, grass everywhere
    SoilParams none;
    none.mud.cover = none.stones.cover = none.sand.cover = 0.f;
    std::vector<uint8_t> empty = SoilField(none).bake(glm::ivec2(2, -3));
    bool clear = true;
    for(size_t i = 0; i < soil::num_texels; i++) {
        for(int kind = 0; kind < soil::kinds; kind++) clear &= empty[i * soil::channels + kind] == 0;
        clear &= empty[i * soil::channels + 3] == 255;
    }
    CHECK(clear);
    CHECK(soilKey(none) != soilKey(SoilParams {}));

    // More cover : a lower threshold
    SoilParams more;
    more.stones.cover = 0.4f;
    CHECK(SoilField(more).threshold(soil::stones) < SoilField().threshold(soil::stones));
    CHECK(SoilField(more).threshold(soil::mud) == SoilField().threshold(soil::mud));
    CHECK(soilKey(more) != soilKey(SoilParams {}));

    // Octaves finer than two texels are left out
    SoilParams fine;
    fine.octaves = 6;
    fine.mud.wavelength = 20.f;
    CHECK(soil::octaves(fine, fine.mud) == 2);      // 20 m and 10 m
    CHECK(soil::octaves(SoilParams {}, SoilParams {}.stones) == 3);
    CHECK(soilKey(fine) != soilKey(SoilParams {}));

    SoilParams seeded;
    seeded.seed = 8;
    CHECK(soilKey(seeded) != soilKey(SoilParams {}));
    CHECK(SoilField(seeded).bake(glm::ivec2(2, -3)) != map);

    SoilParams wide;
    wide.grass_margin = 12.f;
    CHECK(soilKey(wide) != soilKey(SoilParams {}));

    // Full grass away from the patches, none well inside, shorter in between
    CHECK(soil::grassHeight(-20.f, 6.f) == doctest::Approx(1.f));
    CHECK(soil::grassHeight(-6.f, 6.f) == doctest::Approx(1.f));
    CHECK(soil::grassHeight(-3.f, 6.f) < 1.f);
    CHECK(soil::grassHeight(0.f, 6.f) < 0.6f);
    CHECK(soil::grassHeight(0.f, 6.f) > 0.2f);
    CHECK(soil::grassHeight(soil::grass_inside, 6.f) == doctest::Approx(0.f));
    CHECK(soil::grassHeight(30.f, 6.f) == doctest::Approx(0.f));

    // The map is the distance into the patches, 0.5 on their edges
    SoilField field;
    int checked = 0;
    for(int k = 0; k < 4000 && checked < 50; k++) {
        float x = (float) ((k * 37) % 1009) * 4.f, z = (float) ((k * 91) % 1013) * 4.f;
        glm::vec3 distance = field.distance(x, z);
        glm::vec4 sample = field.sample(x, z);
        for(int kind = 0; kind < soil::kinds; kind++) {
            if(std::abs(distance[kind]) > 3.f) continue;
            CHECK(sample[kind] == doctest::Approx(0.5f + distance[kind] / SoilParams {}.edge_width));
            checked++;
        }
    }
    CHECK(checked >= 50);

}

}
