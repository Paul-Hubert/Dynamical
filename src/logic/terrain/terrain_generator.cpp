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

void TerrainGenerator::sample(glm::ivec3 start, glm::ivec3 count, std::vector<float>& values) const {

    std::vector<float> heights((size_t) count.x * count.z);
    surface->GenUniformGrid2D(heights.data(), start.x, start.z, count.x, count.z, height_frequency * chunk::cube_size, seed);

    std::vector<float> detail((size_t) count.x * count.y * count.z);
    overhang->GenUniformGrid3D(detail.data(), start.x, start.y, start.z, count.x, count.y, count.z, overhang_frequency * chunk::cube_size, seed + 1);

    values.resize((size_t) count.x * count.y * count.z);

    for(int x = 0; x < count.x; x++) {
        for(int z = 0; z < count.z; z++) {
            float height = base_level + height_amplitude * heights[(size_t) z * count.x + x];
            for(int y = 0; y < count.y; y++) {
                float world_y = (float) ((start.y + y) * chunk::cube_size);
                // FastNoise grids are x fastest, then y, then z
                float noise = detail[((size_t) z * count.y + y) * count.x + x];
                values[((size_t) x * count.z + z) * count.y + y] = height - world_y + overhang_amplitude * noise;
            }
        }
    }

}

bool TerrainGenerator::generate(glm::ivec3 pos, std::vector<float>& values, float& mean) const {

    // Noise grids are indexed in samples, first sample sits one cube before the chunk origin
    sample(pos * chunk::cubes - chunk::border, glm::ivec3(chunk::values), values);

    bool positive = false, negative = false;
    double sum = 0.;
    for(float value : values) {
        positive |= value > 0.f;
        negative |= value <= 0.f;
        sum += value;
    }

    mean = (float) (sum / (double) values.size());

    return positive && negative;

}

float TerrainGenerator::surfaceHeight(float x, float z) const {
    float scale = height_frequency;
    return base_level + height_amplitude * surface->GenSingle2D(x * scale, z * scale, seed);
}

void TerrainGenerator::surfaceHeights(glm::ivec2 start, glm::ivec2 count, float spacing, std::vector<float>& heights) const {
    heights.resize((size_t) count.x * count.y);
    surface->GenUniformGrid2D(heights.data(), start.x, start.y, count.x, count.y, height_frequency * spacing, seed);
    for(float& height : heights) height = base_level + height_amplitude * height;
}
