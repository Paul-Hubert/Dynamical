#include "grass_height.h"

#include "FastNoise/FastNoise.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace dy;

// Part of the key : to change with anything that changes the maps for the same parameters
constexpr uint32_t baker_version = 1;

struct GrassHeightField::Noise {
    FastNoise::SmartNode<> node;
    float frequency;    // per metre
};

int grass_height::octaves(const GrassHeightParams& params) {
    const float finest = 2.f * chunk::cube_size;
    float wavelength = std::max(params.wavelength, finest);
    float lacunarity = std::max(params.lacunarity, 1.01f);
    int fit = 1 + (int) std::floor(std::log(wavelength / finest) / std::log(lacunarity) + 1e-4f);
    return std::clamp(params.octaves, 1, fit);
}

uint64_t dy::grassHeightKey(const GrassHeightParams& params) {
    uint64_t hash = 1469598103934665603ull;
    auto add = [&](const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for(size_t i = 0; i < size; i++) {
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        }
    };
    add(&baker_version, sizeof(baker_version));
    add(&params.seed, sizeof(params.seed));
    add(&params.wavelength, sizeof(params.wavelength));
    add(&params.octaves, sizeof(params.octaves));
    add(&params.lacunarity, sizeof(params.lacunarity));
    add(&params.gain, sizeof(params.gain));
    add(&params.contrast, sizeof(params.contrast));
    return hash;
}

GrassHeightField::GrassHeightField(const GrassHeightParams& params) : p(params), noise(std::make_unique<Noise>()) {

    auto perlin = FastNoise::New<FastNoise::Perlin>();

    auto fractal = FastNoise::New<FastNoise::FractalFBm>();
    fractal->SetSource(perlin);
    fractal->SetOctaveCount(grass_height::octaves(p));
    fractal->SetGain(std::clamp(p.gain, 0.f, 1.f));
    fractal->SetLacunarity(std::max(p.lacunarity, 1.01f));

    noise->node = fractal;
    noise->frequency = 1.f / std::max(p.wavelength, 2.f * chunk::cube_size);

}

GrassHeightField::~GrassHeightField() = default;

float GrassHeightField::sample(float x, float z) const {
    float value = noise->node->GenSingle2D(x * noise->frequency, z * noise->frequency, p.seed);
    return std::clamp(0.5f + p.contrast * value, 0.f, 1.f);
}

void GrassHeightField::bake(glm::ivec2 chunk, uint8_t* texels) const {

    // Indexed in terrain samples like the densities : neighbours get the same values on their shared edge
    const int size = grass_height::size;
    float values[grass_height::num_texels];
    noise->node->GenUniformGrid2D(values, chunk.x * chunk::cubes, chunk.y * chunk::cubes, size, size, noise->frequency * chunk::cube_size, p.seed);

    for(size_t i = 0; i < grass_height::num_texels; i++) {
        texels[i] = (uint8_t) (std::clamp(0.5f + p.contrast * values[i], 0.f, 1.f) * 255.f + 0.5f);
    }

}

std::vector<uint8_t> GrassHeightField::bake(glm::ivec2 chunk) const {
    std::vector<uint8_t> texels(grass_height::num_texels);
    bake(chunk, texels.data());
    return texels;
}

float dy::sampleGrassHeightMap(const uint8_t* texels, glm::vec2 local) {

    const int size = grass_height::size;
    glm::vec2 at = glm::clamp(local / (float) chunk::cube_size, glm::vec2(0.f), glm::vec2((float) (size - 1)));
    int x = std::min((int) at.x, size - 2), z = std::min((int) at.y, size - 2);
    float fx = at.x - (float) x, fz = at.y - (float) z;

    auto texel = [&](int i, int j) { return (float) texels[(size_t) j * size + i] / 255.f; };
    return glm::mix(glm::mix(texel(x, z), texel(x + 1, z), fx), glm::mix(texel(x, z + 1), texel(x + 1, z + 1), fx), fz);

}
