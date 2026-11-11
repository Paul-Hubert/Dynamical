#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "logic/grass/grass_textures.h"

using namespace dy;

namespace {

struct Random {
    uint32_t seed = 4321;
    float next() {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) * (1.f / 16777216.f);
    }
};

// Through a block and back
void roundTrip(const uint8_t texels[16], uint8_t out[16]) {
    uint8_t block[8];
    encodeBC4(texels, block);
    decodeBC4(block, out);
}

}

TEST_SUITE("unit") {

TEST_CASE("GRASS.8 blocks keep flat texels, two values, the ground and the top, and the rest within a step") {

    uint8_t texels[16], out[16];

    // One value, then two
    for(int value : {0, 1, 128, 255}) {
        std::fill(std::begin(texels), std::end(texels), uint8_t(value));
        roundTrip(texels, out);
        for(int i = 0; i < 16; i++) CHECK(out[i] == value);
    }
    for(int i = 0; i < 16; i++) texels[i] = i % 3 == 0 ? 37 : 201;
    roundTrip(texels, out);
    for(int i = 0; i < 16; i++) CHECK(out[i] == texels[i]);

    // A blade (a ramp) over the ground, next to the top : 0 and 255 stay what they are
    for(int i = 0; i < 16; i++) texels[i] = i < 6 ? 0 : i < 8 ? 255 : uint8_t(100 + (i - 8) * 6);
    roundTrip(texels, out);
    for(int i = 0; i < 16; i++) {
        if(texels[i] == 0 || texels[i] == 255) CHECK(out[i] == texels[i]);
        else CHECK(std::abs(int(out[i]) - int(texels[i])) <= 6);
    }

    // Anything : no worse than the eight values between the lowest and the highest texel
    Random random;
    for(int n = 0; n < 2000; n++) {
        int low = int(random.next() * 256.f), high = int(random.next() * 256.f);
        if(low > high) std::swap(low, high);
        for(int i = 0; i < 16; i++) texels[i] = uint8_t(low + int(random.next() * float(high - low + 1)));
        roundTrip(texels, out);

        double squares = 0.;
        for(int i = 0; i < 16; i++) squares += double(out[i] - texels[i]) * double(out[i] - texels[i]);
        CHECK(std::sqrt(squares / 16.) <= (high - low) / 14. + 0.5);
    }

}

TEST_CASE("GRASS.9 the pass gets the heights apart from the normals and the ids, level after level") {

    GrassBakeParams params;
    params.size = 48;
    params.angles = 8;
    GrassRaycast data = bakeGrass(params, 2);
    const size_t texels = size_t(params.size) * params.size * params.angles;

    // As they are : 48, 24, 12, 6, 3, 1 across
    GrassTextures plain = makeGrassTextures(data.oblique, params.size, params.angles, 7, false);
    CHECK_FALSE(plain.compressed);
    REQUIRE(plain.levels() == 6);
    CHECK(plain.height.level_sizes[0] == texels);
    CHECK(plain.normal.level_sizes[0] == texels * 2);
    CHECK(plain.id.level_sizes[0] == texels);
    CHECK(plain.height.level_sizes[1] == texels / 8);
    CHECK(plain.height.level_sizes[5] == 1);
    for(size_t i = 0; i < texels; i += 97) {
        CHECK(plain.height.data[i] == data.oblique[i * 4]);
        CHECK(plain.normal.data[i * 2] == data.oblique[i * 4 + 1]);
        CHECK(plain.normal.data[i * 2 + 1] == data.oblique[i * 4 + 2]);
        CHECK(plain.id.data[i] == data.oblique[i * 4 + 3]);
    }
    size_t total = 0;
    for(size_t size : plain.normal.level_sizes) total += size;
    CHECK(total == plain.normal.data.size());

    // In blocks : 8 bytes for 16 texels, a level of 6 texels takes 2 blocks across
    GrassTextures blocks = makeGrassTextures(data.oblique, params.size, params.angles, 7, true);
    CHECK(blocks.compressed);
    REQUIRE(blocks.levels() == 6);
    CHECK(blocks.height.level_sizes[0] == texels / 2);
    CHECK(blocks.normal.level_sizes[0] == texels);
    CHECK(blocks.id.level_sizes[0] == texels / 2);
    CHECK(blocks.height.level_sizes[3] == 2 * 2 * 1 * 8);
    CHECK(blocks.height.level_sizes[5] == 8);
    CHECK(blocks.bytes() < plain.bytes() * 0.6);

    // The first block is the first texels of the image
    uint8_t expected[16], block[8];
    for(int j = 0; j < 4; j++) {
        for(int i = 0; i < 4; i++) expected[j * 4 + i] = data.oblique[(size_t(j) * params.size + i) * 4];
    }
    encodeBC4(expected, block);
    CHECK(std::equal(block, block + 8, blocks.height.data.begin()));

    // One level only
    CHECK(makeGrassTextures(data.top, params.size, 1, 1, true).levels() == 1);

}

TEST_CASE("GRASS.10 the rays read back from the blocks are the baked ones a few parts in 255 away") {

    GrassBakeParams params;
    params.size = 48;
    params.angles = 8;
    GrassRaycast data = bakeGrass(params, 2);
    GrassRaycast blocks = compressedGrassRaycast(data);

    REQUIRE(blocks.oblique.size() == data.oblique.size());
    REQUIRE(blocks.top.size() == data.top.size());
    CHECK(blocks.key == data.key);

    double error[4] = {0., 0., 0., 0.};
    const size_t texels = data.oblique.size() / 4;
    for(size_t i = 0; i < data.oblique.size(); i++) {
        error[i % 4] += std::abs(int(data.oblique[i]) - int(blocks.oblique[i]));
    }
    for(int c = 0; c < 4; c++) {
        INFO("channel " << c << " : " << error[c] / texels << " / 255");
        CHECK(error[c] / texels < 8.);
    }

    // The ground between the tufts stays on the ground from the top (a block shared with a blade
    // may lift it by a step of that block)
    int ground = 0, lifted = 0, highest = 0;
    for(size_t i = 0; i < data.top.size(); i += 4) {
        if(data.top[i] != 0) continue;
        ground++;
        lifted += blocks.top[i];
        highest = std::max<int>(highest, blocks.top[i]);
    }
    REQUIRE(ground > 0);
    INFO("ground lifted by " << double(lifted) / ground << " / 255, " << highest << " at most");
    CHECK(double(lifted) / ground < 1.);
    CHECK(highest <= 255 / 14 + 1);

    // The lookup of the shader finds the same heights
    Random random;
    double moved = 0.;
    const int count = 2000;
    for(int i = 0; i < count; i++) {
        float azimuth = random.next() * 6.2831853f;
        glm::vec2 along(std::cos(azimuth), std::sin(azimuth)), entry(random.next(), random.next());
        float reach = 0.3f + 2.f * random.next();
        moved += std::abs(lookupGrass(data, entry, along, reach, 0.f).height - lookupGrass(blocks, entry, along, reach, 0.f).height);
    }
    INFO("mean height moved by " << moved / count);
    CHECK(moved / count < 0.02);

}

}
