#include "height_field.h"

#include <cmath>

#include "scene/world.h"

using namespace dy;

float HeightField::height(float x, float z) const {
    float h = 0.f;
    for(auto& w : wave_list) h += w.amplitude * std::sin(w.k.x * x + w.k.y * z + w.phase);
    return h;
}

void dy::drawWaves(HeightField& field, uint64_t seed) {

    WorldRng rng(seed);
    field.wave_list.clear();
    const double two_pi = 6.283185307179586;
    const double low = std::log(std::max(field.wavelength.x, 1e-3f)), high = std::log(std::max(field.wavelength.y, 1e-3f));

    for(int i = 0; i < field.waves; i++) {
        double wavelength = std::exp(rng.uniform(low, high));
        double k = two_pi / wavelength;
        double direction = rng.uniform(0., two_pi);
        HeightField::Wave w;
        w.k = glm::vec2((float) (k * std::cos(direction)), (float) (k * std::sin(direction)));
        w.amplitude = (float) (field.wave_slope / k);
        w.phase = (float) rng.uniform(0., two_pi);
        field.wave_list.push_back(w);
    }

}

std::vector<float> dy::sampleHeights(const HeightField& field) {
    const float half = field.size() * 0.5f;
    std::vector<float> heights((size_t) field.samples * field.samples);
    for(int z = 0; z < field.samples; z++) {
        for(int x = 0; x < field.samples; x++) {
            heights[(size_t) z * field.samples + x] = field.height(x * field.spacing - half, z * field.spacing - half);
        }
    }
    return heights;
}
