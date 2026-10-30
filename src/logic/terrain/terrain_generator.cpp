#include "terrain_generator.h"

#include "chunk.h"

using namespace dy;

TerrainGenerator::TerrainGenerator(int seed) : seed(seed) {

    auto simplex = FastNoise::New<FastNoise::Simplex>();

    auto fractal = FastNoise::New<FastNoise::FractalFBm>();
    fractal->SetSource(simplex);
    fractal->SetOctaveCount(5);
    fractal->SetGain(0.5f);
    fractal->SetLacunarity(2.0f);
    surface = fractal;

    auto fractal3d = FastNoise::New<FastNoise::FractalFBm>();
    fractal3d->SetSource(simplex);
    fractal3d->SetOctaveCount(3);
    overhang = fractal3d;

}

bool TerrainGenerator::generate(glm::ivec3 pos, std::vector<float>& values, float& mean) const {

    constexpr int n = chunk::values;

    // Noise grids are indexed in samples, first sample sits one cube before the chunk origin
    glm::ivec3 start = pos * chunk::cubes - chunk::border;

    std::vector<float> heights(n * n);
    surface->GenUniformGrid2D(heights.data(), start.x, start.z, n, n, height_frequency * chunk::cube_size, seed);

    std::vector<float> detail((size_t) n * n * n);
    overhang->GenUniformGrid3D(detail.data(), start.x, start.y, start.z, n, n, n, overhang_frequency * chunk::cube_size, seed + 1);

    values.resize(chunk::num_values);

    bool positive = false, negative = false;
    double sum = 0.;

    for(int x = 0; x < n; x++) {
        for(int z = 0; z < n; z++) {
            float height = base_level + height_amplitude * heights[z * n + x];
            for(int y = 0; y < n; y++) {
                float world_y = (float) ((start.y + y) * chunk::cube_size);
                // FastNoise grids are x fastest, then y, then z
                float value = height - world_y + overhang_amplitude * detail[((size_t) z * n + y) * n + x];
                values[chunk::index(x, y, z)] = value;
                positive |= value > 0.f;
                negative |= value <= 0.f;
                sum += value;
            }
        }
    }

    mean = (float) (sum / (double) chunk::num_values);

    return positive && negative;

}

float TerrainGenerator::surfaceHeight(float x, float z) const {
    float scale = height_frequency;
    return base_level + height_amplitude * surface->GenSingle2D(x * scale, z * scale, seed);
}
