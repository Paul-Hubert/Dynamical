#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "logic/grass/grass_height.h"
#include "logic/grass/grass_species.h"
#include "logic/grass/grass_textures.h"
#include "logic/terrain/chunk.h"

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

TEST_CASE("GRASS.13 every kind has its patches and its heights from its own noises, the kinds blend, small plants thin under tall grass") {

    std::vector<int8_t> noise = bakeSpeciesNoise();
    REQUIRE(noise.size() == size_t(grass_species::size) * grass_species::size * 4);
    CHECK(noise == bakeSpeciesNoise());

    // It repeats : the first and the last columns are neighbours, a step of the noise apart
    int jump = 0;
    for(int y = 0; y < grass_species::size; y++) {
        jump = std::max(jump, std::abs(int(noise[size_t(y) * grass_species::size * 4]) - int(noise[(size_t(y) * grass_species::size + grass_species::size - 1) * 4])));
    }
    CHECK(jump < 40);

    const int layers = grass_species::layers;
    const float amount = 0.45f;
    GrassSpeciesParams params, apart, unshaded;
    apart.overlap = 0.f;
    unshaded.shade = 0.f;
    int seen[grass_kinds] = {}, changes = 0, places = 0, thinned = 0, whole = 0, ties = 0;
    int together[layers + 1] = {};
    float largest_step = 0.f, before_keep = 1.f;
    double steps = 0.;
    int step_count = 0, big_steps = 0;
    // Small plants first : under tall grass and under short grass, with their shade and without
    int small_under_tall = 0, small_under_short = 0, plain_under_tall = 0, plain_under_short = 0, under_tall = 0, under_short = 0;
    float shortest[grass_kinds], tallest[grass_kinds];
    std::fill(shortest, shortest + grass_kinds, 1.f);
    std::fill(tallest, tallest + grass_kinds, 0.f);
    GrassKind before = GrassKind::Meadow;
    for(int z = 0; z < 300; z++) {
        for(int x = 0; x < 300; x++) {
            glm::vec2 world(x * 3.f - 450.f, z * 3.f - 450.f);
            GrassSpecies here = grassSpeciesAt(noise, params, world, amount);

            // The first kind keeps half of its plants at least, the second half at most, together all of a tile
            REQUIRE(here.keep[0] >= 0.5f);
            REQUIRE(here.keep[0] <= 1.f);
            int kinds = 1;
            for(int rank = 1; rank < layers; rank++) {
                if(here.kinds[rank] != here.kinds[0]) {
                    REQUIRE(here.keep[rank] > 0.f);
                    REQUIRE(here.keep[rank] <= 0.5f);
                    // In the order of their noises
                    if(rank > 1 && here.kinds[rank - 1] != here.kinds[0]) REQUIRE(here.keep[rank] <= here.keep[rank - 1]);
                    for(int other = 1; other < rank; other++) REQUIRE(here.kinds[other] != here.kinds[rank]);
                    kinds++;
                } else {
                    // The first kind again, no denser than its first layer. No kind after a layer that had none
                    REQUIRE(here.keep[rank] >= 0.f);
                    REQUIRE(here.keep[rank] <= here.keep[0]);
                    for(int after = rank + 1; after < layers; after++) REQUIRE(here.kinds[after] == here.kinds[0]);
                }
            }
            if(kinds > 1) REQUIRE(here.keep[0] + here.keep[1] <= 1.001f);
            together[kinds]++;
            thinned += here.keep[0] < 0.999f;
            whole += here.keep[0] >= 0.999f;

            // As tall as its own noise says, between the shortest and the tallest of its kind
            REQUIRE(here.canopy >= 0.f);
            REQUIRE(here.canopy <= 1.f);
            for(int rank = 0; rank < layers; rank++) {
                // (the last plants of a kind at the edge of its patch are small)
                REQUIRE(here.tall[rank] >= (1.f - amount) * (here.kinds[rank] == here.kinds[0] ? 1.f : grass_species::stunted) - 1e-5f);
                REQUIRE(here.tall[rank] <= 1.f);
                int k = int(here.kinds[rank]);
                shortest[k] = std::min(shortest[k], here.tall[rank]);
                tallest[k] = std::max(tallest[k], here.tall[rank]);
                if(rank > 0 && here.kinds[rank] == here.kinds[0]) REQUIRE(here.tall[rank] == here.tall[0]);
            }

            // No edge : along a row, where the first kind stays the same its plants come and go a few at a time
            if(x > 0 && here.kinds[0] == before) {
                largest_step = std::max(largest_step, std::abs(here.keep[0] - before_keep));
                steps += std::abs(here.keep[0] - before_keep);
                big_steps += std::abs(here.keep[0] - before_keep) > 0.25f;
                step_count++;
            }
            before_keep = here.keep[0];

            // The shade of the tall grass : the same place without it has the same tall grass over it
            GrassSpecies plain = grassSpeciesAt(noise, unshaded, world, amount);
            REQUIRE(plain.canopy == here.canopy);
            if(here.canopy > 0.65f) {
                under_tall++;
                small_under_tall += grassKindInfo(here.kinds[0]).small;
                plain_under_tall += grassKindInfo(plain.kinds[0]).small;
            } else if(here.canopy < 0.35f) {
                under_short++;
                small_under_short += grassKindInfo(here.kinds[0]).small;
                plain_under_short += grassKindInfo(plain.kinds[0]).small;
            }

            // Without overlap : one kind at a place, all of its plants
            GrassSpecies alone = grassSpeciesAt(noise, apart, world, amount);
            // (but where two noises are the same to the byte)
            ties += alone.kinds[1] != alone.kinds[0];
            REQUIRE(alone.kinds[0] == here.kinds[0]);
            if(alone.kinds[1] == alone.kinds[0]) REQUIRE(alone.keep[1] == alone.keep[0]);

            seen[int(here.kinds[0])]++;
            changes += x > 0 && here.kinds[0] != before;
            before = here.kinds[0];
            places++;
        }
    }
    // Each kind takes some of the land, in patches : neighbours 3 m apart are mostly of the same kind.
    // Each grows short somewhere and tall elsewhere
    for(int k = 0; k < grass_kinds; k++) {
        CAPTURE(grassKindInfo(static_cast<GrassKind>(k)).name);
        CHECK(seen[k] > places / 200);
        CHECK(shortest[k] < 1.f - 0.8f * amount);
        CHECK(tallest[k] > 1.f - 0.2f * amount);
    }
    CHECK(changes < places / 4);

    // Blended over a good part of the land, up to four kinds together, whole patches in between
    INFO(together[1] << " places of " << places << " with one kind, " << together[2] << " with two, " << together[3] << " with three, " << together[4]
        << " with four; " << thinned << " with a first kind thinned, " << whole
        << " whole; step of the first kind over 3 m : " << steps / step_count << " on average, " << largest_step << " at most, over a quarter at "
        << big_steps << " places of " << step_count);
    CHECK(places - together[1] > places / 5);
    CHECK(together[3] > places / 50);
    CHECK(together[4] > places / 500);
    CHECK(whole > places / 10);

    // Small plants are first less often under tall grass than under short grass, and than without the shade;
    // more often under short grass than without it. Never nowhere : some are first under tall grass
    INFO("small plants first at " << small_under_tall << " of " << under_tall << " places under tall grass (" << plain_under_tall
        << " without the shade), at " << small_under_short << " of " << under_short << " under short grass (" << plain_under_short << ")");
    REQUIRE(under_tall > places / 50);
    REQUIRE(under_short > places / 50);
    CHECK(small_under_tall * 2 < plain_under_tall);
    CHECK(small_under_short > plain_under_short + under_short / 10);
    CHECK(double(small_under_tall) / under_tall < 0.5 * double(small_under_short) / under_short);
    CHECK(small_under_tall > 0);

    // No edge : a quarter of a metre on, where the first kind is the same, about as many of its plants,
    // about as tall
    float sharpest = 0.f, steepest = 0.f;
    for(int row = 0; row < 40; row++) {
        GrassSpecies last;
        for(int x = 0; x < 2000; x++) {
            glm::vec2 world(x * 0.25f - 250.f, row * 23.f - 450.f);
            GrassSpecies here = grassSpeciesAt(noise, params, world, amount);
            if(x > 0 && here.kinds[0] == last.kinds[0]) {
                sharpest = std::max(sharpest, std::abs(here.keep[0] - last.keep[0]));
                steepest = std::max(steepest, std::abs(here.tall[0] - last.tall[0]));
            }
            last = here;
        }
    }
    INFO("sharpest step of the first kind over 0.25 m : " << sharpest << " of its plants, " << steepest << " of its height");
    CHECK(sharpest < 0.15f);
    CHECK(steepest < 0.02f);
    CHECK(ties < places / 1000);

    // One kind everywhere : all of its plants, as tall as its own noise says
    GrassSpecies forced = grassSpeciesAt(noise, params, glm::vec2(12.f, 7.f), amount, int(GrassKind::Clover));
    for(int rank = 0; rank < layers; rank++) {
        CHECK(forced.kinds[rank] == GrassKind::Clover);
        CHECK(forced.keep[rank] == 1.f);
        CHECK(forced.tall[rank] == forced.tall[0]);
    }

    // No patches : the meadow
    params.size = 0.f;
    CHECK(grassSpeciesAt(noise, params, glm::vec2(12.f, 7.f), amount).kinds[0] == GrassKind::Meadow);
    CHECK(grassSpeciesAt(noise, params, glm::vec2(12.f, 7.f), amount).tall[0] == 1.f);

}

TEST_CASE("GRASS.15 the shell of the grass is at the top of the plants of each place : never under them, low over the small ones") {

    GrassTopParams params;
    // Tall grasses, and small plants a fifth as high
    for(int k = 0; k < grass_kinds; k++) params.heights[k] = grassKindInfo(static_cast<GrassKind>(k)).small ? 0.2f : 0.7f + 0.03f * k;
    params.heights[0] = 1.f;
    GrassTopField field(params);
    CHECK(field.top() == 1.f);
    CHECK(field.key() == GrassTopField(params).key());

    const glm::ivec2 chunk(1, -2);
    const glm::vec2 origin = glm::vec2(chunk) * (float) chunk::length;
    std::vector<uint8_t> map = field.bake(chunk);
    REQUIRE(map.size() == grass_height::num_texels);
    CHECK(map == GrassTopField(params).bake(chunk));

    // The plants of a place are what the species say, the tallest of them
    std::vector<int8_t> noise = bakeSpeciesNoise();
    Random random;
    for(int i = 0; i < 200; i++) {
        glm::vec2 world = origin + glm::vec2(random.next(), random.next()) * (float) chunk::length;
        GrassSpecies here = grassSpeciesAt(noise, params.species, world, params.amount);
        float top = 0.f;
        for(int rank = 0; rank < grass_species::layers; rank++) top = std::max(top, here.tall[rank] * params.heights[int(here.kinds[rank])]);
        REQUIRE(field.sample(world) == top);
    }

    // The shell goes straight between the vertices : over the plants of the chunk, not far over them, and the
    // few that are over it are by little (they are drawn as tall as the shell)
    double over = 0.;
    float under = 0.f, lowest = 1.f, highest = 0.f;
    int count = 0;
    for(int i = 0; i < 20000; i++) {
        glm::vec2 local = glm::vec2(random.next(), random.next()) * (float) chunk::length;
        float shell = sampleGrassHeightMap(map.data(), local) * field.top();
        float plants = field.sample(origin + local);
        under = std::max(under, plants - shell);
        over += shell - plants;
        lowest = std::min(lowest, shell);
        highest = std::max(highest, shell);
        count++;
    }
    INFO("the shell is " << over / count << " over the plants on average, " << under << " under them at worst; from " << lowest << " to " << highest);
    CHECK(under < 0.08f);
    CHECK(over / count < 0.12);
    // Low over the small plants where they grow alone, high over tall grass
    CHECK(lowest < 0.25f);
    CHECK(highest > 0.85f);

    // The same on the edge two chunks share
    std::vector<uint8_t> east = field.bake(chunk + glm::ivec2(1, 0)), south = field.bake(chunk + glm::ivec2(0, 1));
    const int size = grass_height::size;
    for(int i = 0; i < size; i++) {
        REQUIRE(map[(size_t) i * size + size - 1] == east[(size_t) i * size]);
        REQUIRE(map[(size_t) (size - 1) * size + i] == south[i]);
    }

    // Other plants, another map
    GrassTopParams other = params;
    other.species.shade = 0.f;
    CHECK(grassTopKey(other) != grassTopKey(params));
    other = params;
    other.amount = 0.2f;
    CHECK(grassTopKey(other) != grassTopKey(params));
    CHECK(GrassTopField(other).bake(chunk) != map);

    // One kind everywhere : the shell at its height, as its noise says
    GrassTopParams clover = params;
    clover.forced = int(GrassKind::Clover);
    GrassTopField clover_field(clover);
    CHECK(clover_field.top() == doctest::Approx(0.2f));
    CHECK(grassTopKey(clover) != grassTopKey(params));
    for(int i = 0; i < 50; i++) {
        glm::vec2 world = origin + glm::vec2(random.next(), random.next()) * (float) chunk::length;
        float top = clover_field.sample(world);
        REQUIRE(top >= 0.2f * (1.f - clover.amount) - 1e-5f);     // all of its plants : none is small
        REQUIRE(top <= 0.2f);
    }

    // Without the species : the meadow, as tall as its height map
    GrassTopParams meadow = params;
    meadow.species.size = 0.f;
    GrassTopField meadow_field(meadow);
    std::vector<uint8_t> meadow_map = meadow_field.bake(chunk);
    std::vector<uint8_t> heights = GrassHeightField(meadow.height_map).bake(chunk);
    for(size_t i = 0; i < grass_height::num_texels; i += 97) {
        float expected = 1.f - meadow.amount * (1.f - heights[i] / 255.f);
        REQUIRE(std::abs(meadow_map[i] / 255.f - expected) < 1.5f / 255.f);
    }
    // The height map only counts there
    GrassTopParams waved = meadow;
    waved.height_map.wavelength = 50.f;
    CHECK(grassTopKey(waved) != grassTopKey(meadow));
    waved = params;
    waved.height_map.wavelength = 50.f;
    CHECK(grassTopKey(waved) == grassTopKey(params));

}

TEST_CASE("GRASS.14 the plants of a tile stand apart, and so do the first ones of their order") {

    const int count = 100;
    std::vector<glm::vec3> sites = grassTileSites(7, count);
    REQUIRE(sites.size() == count);
    CHECK(sites == grassTileSites(7, count));

    // Nearest neighbour on the tile as it repeats, among the first n plants : a grid would have 1 / sqrt(n)
    auto nearest = [&](int n) {
        float least = 4.f;
        for(int i = 0; i < n; i++) {
            for(int j = 0; j < i; j++) {
                float dx = std::abs(sites[i].x - sites[j].x), dy = std::abs(sites[i].y - sites[j].y);
                dx = std::min(dx, 1.f - dx);
                dy = std::min(dy, 1.f - dy);
                least = std::min(least, std::sqrt(dx * dx + dy * dy));
            }
        }
        return least;
    };
    for(int n : {10, 25, 50, 100}) {
        CAPTURE(n);
        CHECK(nearest(n) > 0.45f / std::sqrt(float(n)));
    }
    for(int i = 0; i < count; i++) {
        CHECK(sites[i].x >= 0.f);
        CHECK(sites[i].x < 1.f);
        CHECK(sites[i].z == doctest::Approx((i + 0.5f) / count));
    }

    // The plants of every kind have those ids : a plant is thinned whole
    for(int kind = 0; kind < grass_kinds; kind++) {
        GrassModelParams params;
        params.kind = static_cast<GrassKind>(kind);
        GrassModel model = generateGrassTile(params);
        float low = 1.f, high = 0.f;
        for(float id : model.ids) {
            low = std::min(low, id);
            high = std::max(high, id);
        }
        CHECK(low < 0.07f);
        CHECK(high > 0.93f);
    }

}

}
