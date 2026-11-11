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

GrassSpecies grassSpeciesAt(const std::vector<int8_t>& noise, const GrassSpeciesParams& params, glm::vec2 world, float tallness) {

    GrassSpecies out;
    if(params.size <= 0.f) return out;
    // Thousandths, as the shader gets it
    const float overlap = std::floor(std::clamp(params.overlap, 0.f, 1.f) * 1000.f) / 1000.f;

    // How much the place is for the tall kinds, -1 to 1 over the band
    const float band = std::max(grass_species::band * overlap, 1e-3f);
    const float tall = std::clamp((tallness - params.low) / band, -1.f, 1.f);

    glm::vec2 at = world / (params.size * grass_species::periods);
    glm::vec4 read[3];
    for(int i = 0; i < 3; i++) read[i] = sample(noise, at * grass_species::scales[i] + grass_species::shifts[i]);

    int kind[3] = {-1, -1, -1}, under = -1;
    float best[3] = {-1e9f, -1e9f, -1e9f}, best_small = -1e9f;
    for(int k = 0; k < grass_kinds; k++) {
        const GrassKindInfo& info = grassKindInfo(static_cast<GrassKind>(k));
        float value = read[k / 4][k % 4] + info.abundance + (info.small ? -tall : tall);
        if(info.small && value > best_small) {
            best_small = value;
            under = k;
        }
        for(int rank = 0; rank < 3; rank++) {
            if(value <= best[rank]) continue;
            for(int moved = 2; moved > rank; moved--) {
                best[moved] = best[moved - 1];
                kind[moved] = kind[moved - 1];
            }
            best[rank] = value;
            kind[rank] = k;
            break;
        }
    }
    const float margin = grass_species::margin * overlap;
    for(int rank = 1; rank < 3; rank++) {
        if(kind[rank] < 0 || best[rank] <= best[0] - margin) kind[rank] = kind[0];
    }
    for(int rank = 0; rank < 3; rank++) out.kinds[rank] = static_cast<GrassKind>(kind[rank]);

    // Small plants under the tall ones in the band only, and nothing under the small ones
    if(grassKindInfo(out.kinds[0]).small) out.under = -2;
    else if(tall >= 1.f || overlap <= 0.f) out.under = -1;
    else out.under = under;
    return out;

}

}
