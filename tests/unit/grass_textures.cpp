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
    params.angles = {8, 8, 8, 8};
    GrassRaycast data = bakeGrass(params, 2);
    const int slices = data.slices();
    const size_t texels = size_t(params.size) * params.size * slices;

    // As they are : 48, 24, 12, 6, 3, 1 across, every level keeps the layers
    GrassTextures plain = makeGrassTextures(data.oblique, params.size, slices, 7, false);
    CHECK_FALSE(plain.compressed);
    REQUIRE(plain.levels() == 6);
    CHECK(plain.height.level_sizes[0] == texels);
    CHECK(plain.normal.level_sizes[0] == texels * 2);
    CHECK(plain.id.level_sizes[0] == texels);
    CHECK(plain.height.level_sizes[1] == texels / 4);
    CHECK(plain.height.level_sizes[5] == size_t(slices));
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
    GrassTextures blocks = makeGrassTextures(data.oblique, params.size, slices, 7, true);
    CHECK(blocks.compressed);
    REQUIRE(blocks.levels() == 6);
    CHECK(blocks.height.level_sizes[0] == texels / 2);
    CHECK(blocks.normal.level_sizes[0] == texels);
    CHECK(blocks.id.level_sizes[0] == texels / 2);
    CHECK(blocks.height.level_sizes[3] == size_t(2 * 2 * slices * 8));
    CHECK(blocks.height.level_sizes[5] == size_t(slices * 8));
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
    params.angles = {8, 8, 8, 8};
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

TEST_CASE("GRASS.12 the colour map holds what each ray met : green by the height, the flowers of the kind, in sRGB and in BC1") {

    // sRGB bytes and back
    for(glm::vec3 colour : {glm::vec3(0.f), glm::vec3(1.f), grass_root_colour, grass_tip_colour, glm::vec3(1.f, 0.8f, 0.08f)}) {
        uint8_t texel[3];
        encodeGrassColour(colour, texel);
        CHECK(glm::length(decodeGrassColour(texel) - colour) < 0.01f);
    }

    GrassBakeParams params;
    params.size = 64;
    params.angles = {8, 8, 8, 8};
    params.model.kind = GrassKind::Dandelion;
    GrassRaycast data = bakeGrass(params, 2);
    REQUIRE(data.oblique_colour.size() == data.oblique.size());
    REQUIRE(data.top_colour.size() == data.top.size());

    // From the top : the colour of what the ray straight down meets, the ground between the plants
    GrassTracer tracer(generateGrassTile(params.model));
    int flowers = 0, ground = 0;
    for(int z = 0; z < params.size; z++) {
        for(int x = 0; x < params.size; x++) {
            GrassTracer::Hit hit;
            REQUIRE(tracer.trace(glm::vec3((x + 0.5f) / params.size, 1.f, (z + 0.5f) / params.size), glm::vec3(0.f, -1.f, 0.f), hit));
            const uint8_t* texel = &data.top_colour[(size_t(z) * params.size + x) * 4];
            uint8_t expected[3];
            encodeGrassColour(hit.colour, expected);
            REQUIRE(std::equal(expected, expected + 3, texel));
            REQUIRE(texel[3] == 255);
            flowers += hit.blade && hit.part == GrassPart::Flower;
            if(!hit.blade) {
                ground++;
                REQUIRE(hit.colour == grass_root_colour);
            }
        }
    }
    CHECK(flowers > 3);
    CHECK(ground > 100);

    // The oblique rays see the yellow too, and the lookup gives it back
    int yellow = 0;
    for(size_t i = 0; i < data.oblique_colour.size(); i += 4) {
        const uint8_t* t = &data.oblique_colour[i];
        yellow += t[0] > 200 && t[1] > 180 && t[2] < 120;
    }
    CHECK(yellow > 50);
    glm::vec3 looked = lookupGrass(data, glm::vec2(0.3f, 0.6f), glm::vec2(1.f, 0.f), 1.f, 0.f).colour;
    CHECK(glm::all(glm::greaterThanEqual(looked, glm::vec3(0.f))));
    CHECK(looked.g > looked.b);

    // The texture of the pass : RGBA8, or 8 bytes for 16 texels, level after level
    const size_t texels = size_t(params.size) * params.size * data.slices();
    GrassTexture plain = makeGrassColour(data.oblique_colour, params.size, data.slices(), 7, false);
    GrassTexture blocks = makeGrassColour(data.oblique_colour, params.size, data.slices(), 7, true);
    REQUIRE(plain.level_sizes.size() == 7);
    REQUIRE(blocks.level_sizes.size() == 7);
    CHECK(plain.level_sizes[0] == texels * 4);
    CHECK(blocks.level_sizes[0] == texels / 2);
    CHECK(blocks.level_sizes[6] == size_t(data.slices()) * 8);
    CHECK(std::equal(data.oblique_colour.begin(), data.oblique_colour.begin() + 64, plain.data.begin()));

    // A block of two colours comes back as it was, to the bits of 5:6:5
    uint8_t two[64], block[8], back[64];
    for(int i = 0; i < 16; i++) {
        const uint8_t green[4] = {40, 120, 30, 255}, flower[4] = {250, 210, 60, 255};
        std::copy(i % 3 ? green : flower, (i % 3 ? green : flower) + 4, &two[i * 4]);
    }
    encodeBC1(two, block);
    decodeBC1(block, back);
    for(int i = 0; i < 64; i++) CHECK(std::abs(int(back[i]) - int(two[i])) <= 8);

    // The whole map through the blocks : a few parts in 255 away
    GrassRaycast read_back = compressedGrassRaycast(data);
    double error = 0.;
    for(size_t i = 0; i < data.oblique_colour.size(); i++) error += std::abs(int(data.oblique_colour[i]) - int(read_back.oblique_colour[i]));
    INFO("colour moved by " << error / (data.oblique_colour.size() * 0.75) << " / 255 in blocks");
    CHECK(error / (data.oblique_colour.size() * 0.75) < 6.);

}

}
