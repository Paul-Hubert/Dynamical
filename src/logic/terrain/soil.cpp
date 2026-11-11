#include "soil.h"

#include "FastNoise/FastNoise.h"

#include <algorithm>
#include <cmath>

using namespace dy;

// Part of the key : to change with anything that changes the maps for the same parameters
constexpr uint32_t baker_version = 4;

// The noise is read on this many points a side, far apart, to find the value that `cover` of them are over
constexpr int calibration_size = 128;
constexpr float calibration_step = 0.83f;   // wavelengths

// Slopes of the noise are taken over two terrain samples, the same from a map and from a single point
constexpr float slope_step = (float) chunk::cube_size;

namespace {

const SoilPatchParams& patchOf(const SoilParams& params, int kind) {
    return kind == soil::mud ? params.mud : kind == soil::stones ? params.stones : params.sand;
}

float wavelengthOf(const SoilPatchParams& patch) {
    return std::max(patch.wavelength, 2.f * chunk::cube_size);
}

// Metres into a patch, from the noise (over its threshold inside) and its values a step to each side
float distanceOf(float value, float west, float east, float north, float south, float threshold, float wavelength) {
    glm::vec2 slope = glm::vec2(east - west, south - north) / (2.f * slope_step);
    // Where the noise is flat an edge is far, or about to appear : not closer than a slope of the
    // whole range over a few wavelengths says
    float steep = std::max(glm::length(slope), 0.05f / wavelength);
    return std::clamp((value - threshold) / steep, -SoilField::max_distance, SoilField::max_distance);
}

}

struct SoilField::Noise {
    struct Patch {
        FastNoise::SmartNode<> node;
        float frequency;    // per metre
        float wavelength;
        float threshold;
        int seed;
    };
    Patch patches[soil::kinds];
};

int soil::octaves(const SoilParams& params, const SoilPatchParams& patch) {
    const float finest = 2.f * chunk::cube_size;
    float lacunarity = std::max(params.lacunarity, 1.01f);
    int fit = 1 + (int) std::floor(std::log(wavelengthOf(patch) / finest) / std::log(lacunarity) + 1e-4f);
    return std::clamp(params.octaves, 1, fit);
}

float soil::grassHeight(float inside, float margin) {
    margin = std::max(margin, 0.f);
    float t = std::clamp((inside + margin) / (margin + grass_inside), 0.f, 1.f);
    return 1.f - t * t * (3.f - 2.f * t);
}

uint64_t dy::soilKey(const SoilParams& params) {
    uint64_t hash = 1469598103934665603ull;
    auto add = [&](const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for(size_t i = 0; i < size; i++) {
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        }
    };
    add(&baker_version, sizeof(baker_version));
    add(&params.seed, sizeof(params.seed));
    add(&params.octaves, sizeof(params.octaves));
    add(&params.lacunarity, sizeof(params.lacunarity));
    add(&params.gain, sizeof(params.gain));
    add(&params.edge_width, sizeof(params.edge_width));
    add(&params.grass_margin, sizeof(params.grass_margin));
    for(int kind = 0; kind < soil::kinds; kind++) {
        const SoilPatchParams& patch = patchOf(params, kind);
        add(&patch.wavelength, sizeof(patch.wavelength));
        add(&patch.cover, sizeof(patch.cover));
    }
    return hash;
}

SoilField::SoilField(const SoilParams& params) : p(params), noise(std::make_unique<Noise>()) {

    std::vector<float> values((size_t) calibration_size * calibration_size);

    for(int kind = 0; kind < soil::kinds; kind++) {
        const SoilPatchParams& patch = patchOf(p, kind);
        Noise::Patch& out = noise->patches[kind];

        auto perlin = FastNoise::New<FastNoise::Perlin>();
        auto fractal = FastNoise::New<FastNoise::FractalFBm>();
        fractal->SetSource(perlin);
        fractal->SetOctaveCount(soil::octaves(p, patch));
        fractal->SetGain(std::clamp(p.gain, 0.f, 1.f));
        fractal->SetLacunarity(std::max(p.lacunarity, 1.01f));

        out.node = fractal;
        out.wavelength = wavelengthOf(patch);
        out.frequency = 1.f / out.wavelength;
        out.seed = p.seed + 101 * kind;

        // What part of the world is over a value depends on the octaves : measured, not assumed
        float cover = std::clamp(patch.cover, 0.f, 1.f);
        if(cover <= 0.f) {
            out.threshold = 1e9f;
        } else {
            out.node->GenUniformGrid2D(values.data(), 0, 0, calibration_size, calibration_size, calibration_step, out.seed);
            size_t rank = std::min((size_t) ((1.f - cover) * (float) values.size()), values.size() - 1);
            std::nth_element(values.begin(), values.begin() + rank, values.end());
            out.threshold = values[rank];
        }
    }

}

SoilField::~SoilField() = default;

float SoilField::threshold(soil::Kind kind) const {
    return noise->patches[kind].threshold;
}

glm::vec3 SoilField::distance(float x, float z) const {
    glm::vec3 out;
    for(int kind = 0; kind < soil::kinds; kind++) {
        const Noise::Patch& patch = noise->patches[kind];
        auto at = [&](float px, float pz) { return patch.node->GenSingle2D(px * patch.frequency, pz * patch.frequency, patch.seed); };
        out[kind] = distanceOf(at(x, z), at(x - slope_step, z), at(x + slope_step, z), at(x, z - slope_step), at(x, z + slope_step),
            patch.threshold, patch.wavelength);
    }
    return out;
}

glm::vec4 SoilField::texel(const glm::vec3& distance) const {
    const float width = std::max(p.edge_width, 0.5f);
    float closest = std::max(distance.x, std::max(distance.y, distance.z));
    return glm::vec4(glm::clamp(0.5f + distance / width, 0.f, 1.f), soil::grassHeight(closest, p.grass_margin));
}

glm::vec4 SoilField::sample(float x, float z) const {
    return texel(distance(x, z));
}

void SoilField::bake(glm::ivec2 chunk, uint8_t* texels) const {

    // Indexed in terrain samples like the densities : neighbours get the same values where they overlap.
    // One more sample on each side, for the slopes
    const int size = soil::size, wide = size + 2;
    std::vector<float> values((size_t) wide * wide);
    std::vector<glm::vec3> distances(soil::num_texels);

    for(int kind = 0; kind < soil::kinds; kind++) {
        const Noise::Patch& patch = noise->patches[kind];
        patch.node->GenUniformGrid2D(values.data(), chunk.x * chunk::cubes - soil::border - 1, chunk.y * chunk::cubes - soil::border - 1,
            wide, wide, patch.frequency * chunk::cube_size, patch.seed);
        for(int j = 0; j < size; j++) {
            const float* row = &values[(size_t) (j + 1) * wide + 1];
            for(int i = 0; i < size; i++) {
                distances[(size_t) j * size + i][kind] = distanceOf(row[i], row[i - 1], row[i + 1], row[i - wide], row[i + wide],
                    patch.threshold, patch.wavelength);
            }
        }
    }

    for(size_t i = 0; i < soil::num_texels; i++) {
        glm::vec4 value = texel(distances[i]);
        for(int channel = 0; channel < soil::channels; channel++) {
            texels[i * soil::channels + channel] = (uint8_t) (value[channel] * 255.f + 0.5f);
        }
    }

}

std::vector<uint8_t> SoilField::bake(glm::ivec2 chunk) const {
    std::vector<uint8_t> texels(soil::num_texels * soil::channels);
    bake(chunk, texels.data());
    return texels;
}

glm::vec4 dy::sampleSoilMap(const uint8_t* texels, glm::vec2 local) {

    const int size = soil::size;
    glm::vec2 at = glm::clamp(local / (float) chunk::cube_size + (float) soil::border, glm::vec2(0.f), glm::vec2((float) (size - 1)));
    int x = std::min((int) at.x, size - 2), z = std::min((int) at.y, size - 2);
    float fx = at.x - (float) x, fz = at.y - (float) z;

    auto texel = [&](int i, int j) {
        const uint8_t* t = texels + ((size_t) j * size + i) * soil::channels;
        return glm::vec4(t[0], t[1], t[2], t[3]) / 255.f;
    };
    return glm::mix(glm::mix(texel(x, z), texel(x + 1, z), fx), glm::mix(texel(x, z + 1), texel(x + 1, z + 1), fx), fz);

}
