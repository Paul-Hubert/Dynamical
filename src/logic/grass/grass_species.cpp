#include "grass_species.h"

#include <algorithm>
#include <cmath>

namespace dy {

namespace {

uint32_t hash(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Perlin noise that repeats every `period` cells, in about -1..1
float perlin(float x, float y, int period, uint32_t seed) {
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    float fx = x - x0, fy = y - y0;
    auto corner = [&](int i, int j) {
        uint32_t h = hash(uint32_t(((x0 + i) % period + period) % period), uint32_t(((y0 + j) % period + period) % period), seed);
        float angle = (h & 0xffff) * (6.28318530718f / 65536.f);
        return std::cos(angle) * (fx - i) + std::sin(angle) * (fy - j);
    };
    float u = fx * fx * fx * (fx * (fx * 6.f - 15.f) + 10.f), v = fy * fy * fy * (fy * (fy * 6.f - 15.f) + 10.f);
    return 1.41421356f * glm::mix(glm::mix(corner(0, 0), corner(1, 0), u), glm::mix(corner(0, 1), corner(1, 1), u), v);
}

int wrap(int i, int n) {
    return ((i % n) + n) % n;
}

// The texture as the shader reads it : linear, repeated, -1..1
glm::vec4 sample(const std::vector<int8_t>& noise, glm::vec2 uv) {
    const int n = grass_species::size;
    float fx = uv.x * n - 0.5f, fy = uv.y * n - 0.5f;
    int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    float tx = fx - x0, ty = fy - y0;
    auto texel = [&](int x, int y) {
        const int8_t* t = &noise[(size_t(wrap(y, n)) * n + wrap(x, n)) * 4];
        return glm::max(glm::vec4(t[0], t[1], t[2], t[3]) / 127.f, glm::vec4(-1.f));
    };
    return glm::mix(glm::mix(texel(x0, y0), texel(x0 + 1, y0), tx), glm::mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), tx), ty);
}

// Part of the key of the maps : to change with anything that changes them for the same parameters
constexpr uint32_t top_baker_version = 1;

// The top of the plants is sampled every half cube of the terrain, this many samples around the chunk too :
// a texel is the highest of those within `top_reach` of its vertex
constexpr int top_reach = 1;

}

std::vector<int8_t> bakeSpeciesNoise(uint32_t seed) {

    const int n = grass_species::size;
    std::vector<int8_t> out(size_t(n) * n * 4);
    for(int y = 0; y < n; y++) {
        for(int x = 0; x < n; x++) {
            for(int c = 0; c < 4; c++) {
                float value = 0.f, weight = 1.f, total = 0.f;
                int period = grass_species::periods;
                for(int octave = 0; octave < grass_species::octaves; octave++) {
                    value += weight * perlin((x + 0.5f) / n * period, (y + 0.5f) / n * period, period, seed + uint32_t(c * 101 + octave * 7));
                    total += weight;
                    weight *= 0.4f;
                    period *= 2;
                }
                // Octaves of Perlin noise seldom leave the middle half of their range : twice the contrast
                out[(size_t(y) * n + x) * 4 + c] = int8_t(std::lround(std::clamp(2.f * value / total, -1.f, 1.f) * 127.f));
            }
        }
    }
    return out;

}

GrassSpecies grassSpeciesAt(const std::vector<int8_t>& noise, const GrassSpeciesParams& params, glm::vec2 world, float amount, int forced) {

    GrassSpecies out;
    const int layers = grass_species::layers;
    if(forced >= 0) {
        for(int rank = 0; rank < layers; rank++) out.kinds[rank] = static_cast<GrassKind>(std::min(forced, grass_kinds - 1));
    }
    // No patches : the meadow, as tall as the height map of the grass says (GrassHeightField)
    if(params.size <= 0.f) return out;

    glm::vec2 at = world / (params.size * grass_species::periods);
    glm::vec4 much[3], high[3];
    for(int i = 0; i < 3; i++) {
        much[i] = sample(noise, at * grass_species::scales[i] + grass_species::shifts[i]);
        high[i] = sample(noise, at * grass_species::height_scales[i] + grass_species::height_shifts[i]);
    }
    // How tall a kind grows here, 0 its shortest to 1 its tallest
    auto grown = [&](int k) { return std::clamp(0.5f + grass_species::height_contrast * high[k / 4][k % 4], 0.f, 1.f); };
    amount = std::clamp(amount, 0.f, 1.f);
    auto tall = [&](int k) { return 1.f - amount * (1.f - grown(k)); };

    if(forced >= 0) {
        const int k = static_cast<int>(out.kinds[0]);
        for(int rank = 0; rank < layers; rank++) out.tall[rank] = tall(k);
        out.canopy = grown(k);
        return out;
    }

    // Thousandths, as the shader gets it
    const float overlap = std::floor(std::clamp(params.overlap, 0.f, 1.f) * 1000.f) / 1000.f;
    const float width = std::max(grass_species::margin * overlap, 1e-6f);

    // How tall the tall kinds of the place grow : those whose noise is close to the best tall one's
    float value[grass_kinds], best_tall = -1e9f;
    for(int k = 0; k < grass_kinds; k++) {
        const GrassKindInfo& info = grassKindInfo(static_cast<GrassKind>(k));
        value[k] = much[k / 4][k % 4] + info.abundance;
        if(!info.small) best_tall = std::max(best_tall, value[k]);
    }
    float canopy = 0.f, weights = 0.f;
    for(int k = 0; k < grass_kinds; k++) {
        if(grassKindInfo(static_cast<GrassKind>(k)).small) continue;
        float weight = std::clamp(1.f + (value[k] - best_tall) / grass_species::canopy_width, 0.f, 1.f);
        canopy += weight * grown(k);
        weights += weight;
    }
    out.canopy = weights > 0.f ? canopy / weights : 0.5f;
    // In their shade the small kinds have less of their noise, and more of it where the grass stays short
    const float shade = params.shade * (1.f - 2.f * out.canopy);

    // One more than the layers : the best of the kinds that are not one
    int kind[grass_species::layers + 1];
    float best[grass_species::layers + 1];
    for(int rank = 0; rank <= layers; rank++) {
        kind[rank] = -1;
        best[rank] = -1e9f;
    }
    for(int k = 0; k < grass_kinds; k++) {
        float v = value[k] + (grassKindInfo(static_cast<GrassKind>(k)).small ? shade : 0.f);
        for(int rank = 0; rank <= layers; rank++) {
            if(v <= best[rank]) continue;
            for(int moved = layers; moved > rank; moved--) {
                best[moved] = best[moved - 1];
                kind[moved] = kind[moved - 1];
            }
            best[rank] = v;
            kind[rank] = k;
            break;
        }
    }

    // The first kind against the second, the others against the first : all of its plants half a blend
    // width inside its patch, none as far outside
    out.keep[0] = std::clamp(0.5f + (best[0] - best[1]) / width, 0.f, 1.f);
    out.kinds[0] = static_cast<GrassKind>(kind[0]);
    out.tall[0] = tall(kind[0]);
    for(int rank = 1; rank < layers; rank++) {
        const float against = 0.5f + (best[rank] - best[0]) / width;
        // No plant left where the next kind takes its place among the layers
        const float leaving = (best[rank] - best[layers]) / (grass_species::fade * width);
        const float own = kind[rank] < 0 ? 0.f : std::clamp(std::min(against, leaving), 0.f, 1.f);
        if(own > 0.f) {
            out.kinds[rank] = static_cast<GrassKind>(kind[rank]);
            out.keep[rank] = own;
            // The last plants of a kind at the edge of its patch are small
            out.tall[rank] = tall(kind[rank]) * glm::mix(grass_species::stunted, 1.f, glm::smoothstep(0.f, 0.5f, own));
        } else {
            // Where no other kind is close the layers are all of the first one : none of it where the kind of
            // the layer has just gone, as dense as the first layer half a blend width further
            out.kinds[rank] = out.kinds[0];
            out.keep[rank] = out.keep[0] * std::clamp(-2.f * against, 0.f, 1.f);
            out.tall[rank] = out.tall[0];
        }
    }
    return out;

}

GrassTopField::GrassTopField(const GrassTopParams& params) : p(params), noise(bakeSpeciesNoise()), meadow(params.height_map) {

    p.forced = std::min(p.forced, grass_kinds - 1);
    if(p.forced >= 0 || p.species.size <= 0.f) {
        highest = p.heights[std::max(p.forced, 0)];
    } else {
        highest = *std::max_element(p.heights.begin(), p.heights.end());
    }
    highest = std::max(highest, 1e-3f);

}

uint64_t grassTopKey(const GrassTopParams& p) {
    uint64_t hash = 1469598103934665603ull;
    auto add = [&](const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for(size_t i = 0; i < size; i++) {
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        }
    };
    add(&top_baker_version, sizeof(top_baker_version));
    add(&p.species.size, sizeof(float));
    add(&p.species.overlap, sizeof(float));
    add(&p.species.shade, sizeof(float));
    add(&p.amount, sizeof(float));
    add(&p.forced, sizeof(int));
    add(p.heights.data(), sizeof(float) * p.heights.size());
    // The height map of the meadow only counts without the species
    if(p.species.size <= 0.f) {
        const uint64_t height_key = grassHeightKey(p.height_map);
        add(&height_key, sizeof(height_key));
    }
    return hash;
}

float GrassTopField::sample(glm::vec2 world) const {

    if(p.species.size <= 0.f) {
        return highest * (1.f - std::clamp(p.amount, 0.f, 1.f) * (1.f - meadow.sample(world.x, world.y)));
    }
    GrassSpecies here = grassSpeciesAt(noise, p.species, world, p.amount, p.forced);
    float top = 0.f;
    for(int rank = 0; rank < grass_species::layers; rank++) {
        if(here.keep[rank] > 0.f) top = std::max(top, here.tall[rank] * p.heights[static_cast<int>(here.kinds[rank])]);
    }
    return top;

}

void GrassTopField::bake(glm::ivec2 chunk, uint8_t* texels) const {

    const int size = grass_height::size;
    // Rounded up : the shell is never under the plants
    auto texel = [&](float top) { return (uint8_t) std::clamp(std::ceil(top / highest * 255.f - 1e-3f), 0.f, 255.f); };

    // The meadow alone : its height map, which has nothing finer than the terrain vertices
    if(p.species.size <= 0.f) {
        meadow.bake(chunk, texels);
        const float amount = std::clamp(p.amount, 0.f, 1.f);
        for(size_t i = 0; i < grass_height::num_texels; i++) {
            texels[i] = texel(highest * (1.f - amount * (1.f - (float) texels[i] / 255.f)));
        }
        return;
    }

    // Sampled at places that only depend on the world : neighbours get the same values on their shared edge
    const int n = 2 * (size - 1) + 1 + 2 * top_reach;
    const float step = 0.5f * chunk::cube_size;
    std::vector<float> tops((size_t) n * n), rows((size_t) n * size);
    for(int j = 0; j < n; j++) {
        for(int i = 0; i < n; i++) {
            glm::ivec2 half = 2 * chunk * (int) chunk::cubes + glm::ivec2(i, j) - top_reach;
            tops[(size_t) j * n + i] = sample(glm::vec2(half) * step);
        }
    }
    for(int j = 0; j < n; j++) {
        for(int i = 0; i < size; i++) {
            float top = 0.f;
            for(int k = 0; k <= 2 * top_reach; k++) top = std::max(top, tops[(size_t) j * n + 2 * i + k]);
            rows[(size_t) j * size + i] = top;
        }
    }
    for(int j = 0; j < size; j++) {
        for(int i = 0; i < size; i++) {
            float top = 0.f;
            for(int k = 0; k <= 2 * top_reach; k++) top = std::max(top, rows[(size_t) (2 * j + k) * size + i]);
            texels[(size_t) j * size + i] = texel(top);
        }
    }

}

std::vector<uint8_t> GrassTopField::bake(glm::ivec2 chunk) const {
    std::vector<uint8_t> texels(grass_height::num_texels);
    bake(chunk, texels.data());
    return texels;
}

}
