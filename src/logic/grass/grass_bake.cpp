#include "grass_bake.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <thread>

namespace dy {

namespace {

const float two_pi = 6.28318530718f;
const float quarter_pi = 0.78539816339f;

// Bump when the baker or the meaning of the texels changes : cached files become stale
const uint32_t bake_version = 2;
const char file_magic[8] = {'D', 'Y', 'G', 'R', 'A', 'S', 'S', '\0'};

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// Möller-Trumbore, both sides
bool intersect(glm::vec3 o, glm::vec3 d, glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, float& t, float& u, float& v) {
    glm::vec3 e1 = v1 - v0, e2 = v2 - v0;
    glm::vec3 p = glm::cross(d, e2);
    float det = glm::dot(e1, p);
    if(std::abs(det) < 1e-12f) return false;
    float inv = 1.f / det;
    glm::vec3 s = o - v0;
    u = glm::dot(s, p) * inv;
    if(u < 0.f || u > 1.f) return false;
    glm::vec3 q = glm::cross(s, e1);
    v = glm::dot(d, q) * inv;
    if(v < 0.f || u + v > 1.f) return false;
    t = glm::dot(e2, q) * inv;
    return true;
}

void encode(uint8_t* texel, float height, glm::vec3 normal, float id) {
    auto byte = [](float value) {
        return static_cast<uint8_t>(std::lround(std::clamp(value, 0.f, 1.f) * 255.f));
    };
    glm::vec2 encoded = encodeGrassNormal(normal);
    texel[0] = byte(height);
    texel[1] = byte(encoded.x);
    texel[2] = byte(encoded.y);
    texel[3] = byte(id);
}

glm::vec4 decode(const uint8_t* texel) {
    return glm::vec4(texel[0], texel[1], texel[2], texel[3]) / 255.f;
}

int wrap(int i, int n) {
    return ((i % n) + n) % n;
}

float smoothstep(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}

glm::vec2 encodeGrassNormal(glm::vec3 n) {
    n /= std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
    glm::vec2 e(n.x, n.z);
    if(n.y < 0.f) {
        e = glm::vec2((1.f - std::abs(e.y)) * (e.x >= 0.f ? 1.f : -1.f), (1.f - std::abs(e.x)) * (e.y >= 0.f ? 1.f : -1.f));
    }
    return e * 0.5f + 0.5f;
}

glm::vec3 decodeGrassNormal(glm::vec2 encoded) {
    glm::vec2 e = encoded * 2.f - 1.f;
    glm::vec3 n(e.x, 1.f - std::abs(e.x) - std::abs(e.y), e.y);
    float fold = std::max(-n.y, 0.f);
    n.x += n.x >= 0.f ? -fold : fold;
    n.z += n.z >= 0.f ? -fold : fold;
    return glm::normalize(n);
}

GrassTracer::GrassTracer(GrassModel model, int cells_) : tile(std::move(model)), cells(std::max(cells_, 1)), grid(size_t(cells) * cells) {

    for(uint32_t i = 0; i + 2 < tile.indices.size(); i += 3) {
        glm::vec3 a = tile.vertices[tile.indices[i]], b = tile.vertices[tile.indices[i + 1]], c = tile.vertices[tile.indices[i + 2]];
        int x0 = int(std::floor(std::min({a.x, b.x, c.x}) * cells)), x1 = int(std::floor(std::max({a.x, b.x, c.x}) * cells));
        int z0 = int(std::floor(std::min({a.z, b.z, c.z}) * cells)), z1 = int(std::floor(std::max({a.z, b.z, c.z}) * cells));
        for(int gz = z0; gz <= z1; gz++) {
            for(int gx = x0; gx <= x1; gx++) {
                // The copy of the tile that brings this part of the triangle back in the tile
                int tx = floorDiv(gx, cells), tz = floorDiv(gz, cells);
                grid[size_t(gz - tz * cells) * cells + (gx - tx * cells)].push_back({i, int16_t(-tx), int16_t(-tz)});
            }
        }
    }

}

bool GrassTracer::trace(glm::vec3 o, glm::vec3 d, Hit& hit, float max_t) const {

    if(o.y < 0.f && d.y <= 0.f) return false;

    float best = max_t;
    bool found = false;
    if(d.y < 0.f) {
        float t = -o.y / d.y;
        if(t < best) {
            best = t;
            hit.t = t;
            hit.normal = glm::vec3(0.f, 1.f, 0.f);
            hit.blade = false;
            hit.id = 0.5f;
            found = true;
        }
    }

    // Blades are between the ground and max_height
    float t0 = 0.f, t1 = best;
    if(d.y < 0.f) {
        t0 = std::max(t0, (tile.max_height - o.y) / d.y);
    } else if(d.y > 0.f) {
        t0 = std::max(t0, -o.y / d.y);
        t1 = std::min(t1, (tile.max_height - o.y) / d.y);
    } else if(o.y > tile.max_height) {
        return found;
    }
    if(t0 >= t1) return found;

    // Walk the cells under the ray, tile after tile
    const float inf = std::numeric_limits<float>::infinity();
    glm::vec2 p = (glm::vec2(o.x, o.z) + glm::vec2(d.x, d.z) * t0) * float(cells);
    int ix = int(std::floor(p.x)), iz = int(std::floor(p.y));
    float dx = d.x * cells, dz = d.z * cells;
    int step_x = dx > 0.f ? 1 : -1, step_z = dz > 0.f ? 1 : -1;
    float delta_x = dx != 0.f ? std::abs(1.f / dx) : inf, delta_z = dz != 0.f ? std::abs(1.f / dz) : inf;
    float next_x = dx != 0.f ? t0 + ((dx > 0.f ? ix + 1 : ix) - p.x) / dx : inf;
    float next_z = dz != 0.f ? t0 + ((dz > 0.f ? iz + 1 : iz) - p.y) / dz : inf;

    const float eps = 1e-5f;
    float blade_t = inf, blade_u = 0.f, blade_v = 0.f;
    uint32_t blade = 0;

    for(int guard = 0; guard < 1000000; guard++) {

        float exit = std::min({next_x, next_z, t1});
        int tx = floorDiv(ix, cells), tz = floorDiv(iz, cells);

        for(const Entry& entry : grid[size_t(iz - tz * cells) * cells + (ix - tx * cells)]) {
            glm::vec3 local = o - glm::vec3(tx + entry.shift_x, 0.f, tz + entry.shift_z);
            float t, u, v;
            if(intersect(local, d, tile.vertices[tile.indices[entry.triangle]], tile.vertices[tile.indices[entry.triangle + 1]],
                    tile.vertices[tile.indices[entry.triangle + 2]], t, u, v)
                    && t >= t0 - eps && t <= exit + eps && t < blade_t) {
                blade_t = t;
                blade_u = u;
                blade_v = v;
                blade = entry.triangle;
            }
        }

        if(blade_t < inf || exit >= t1) break;

        if(next_x < next_z) {
            ix += step_x;
            next_x += delta_x;
        } else {
            iz += step_z;
            next_z += delta_z;
        }

    }

    if(blade_t == inf) return found;

    glm::vec3 normal = tile.normals[tile.indices[blade]] * (1.f - blade_u - blade_v)
        + tile.normals[tile.indices[blade + 1]] * blade_u + tile.normals[tile.indices[blade + 2]] * blade_v;
    float length = glm::length(normal);
    normal = length > 0.f ? normal / length : glm::vec3(0.f, 1.f, 0.f);
    if(glm::dot(normal, d) > 0.f) normal = -normal;

    hit.t = std::max(blade_t, 0.f);
    hit.normal = normal;
    hit.blade = true;
    hit.id = tile.ids[tile.indices[blade]];
    return true;

}

uint64_t grassBakeKey(const GrassBakeParams& params) {

    uint64_t hash = 14695981039346656037ull;
    auto add = [&](uint32_t value) {
        for(int i = 0; i < 4; i++) {
            hash ^= (value >> (8 * i)) & 0xff;
            hash *= 1099511628211ull;
        }
    };
    auto add_float = [&](float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        add(bits);
    };

    add(bake_version);
    add(uint32_t(params.size));
    add(uint32_t(params.angles));
    const GrassModelParams& m = params.model;
    add(m.seed);
    add(uint32_t(m.tufts));
    add(uint32_t(m.blades_min));
    add(uint32_t(m.blades_max));
    add(uint32_t(m.segments));
    for(float value : {m.height_min, m.height_max, m.max_height, m.width, m.lean, m.fold}) add_float(value);
    return hash;

}

GrassRaycast bakeGrass(const GrassBakeParams& params, unsigned threads) {

    const int size = std::max(params.size, 1), angles = std::max(params.angles, 1);

    GrassRaycast out;
    out.size = size;
    out.angles = angles;
    out.key = grassBakeKey(params);
    out.oblique.resize(size_t(angles) * size * size * 4);
    out.top.resize(size_t(size) * size * 4);

    GrassTracer tracer(generateGrassTile(params.model));

    // Slice `angles` is the top down one. Rays lose one unit of height per unit of t : they start at
    // y = 1, above every blade, and the 45 degree ones are over their texel when half way down.
    auto slice = [&](int a) {
        bool top = a == angles;
        float azimuth = (a + 0.5f) / angles * two_pi;
        glm::vec2 along = top ? glm::vec2(0.f) : glm::vec2(std::cos(azimuth), std::sin(azimuth));
        glm::vec3 direction(along.x, -1.f, along.y);
        uint8_t* texels = top ? out.top.data() : out.oblique.data() + size_t(a) * size * size * 4;

        for(int z = 0; z < size; z++) {
            for(int x = 0; x < size; x++) {
                glm::vec2 q((x + 0.5f) / size, (z + 0.5f) / size);
                glm::vec3 origin(q.x - 0.5f * along.x, 1.f, q.y - 0.5f * along.y);
                GrassTracer::Hit hit;
                tracer.trace(origin, direction, hit);
                encode(texels + (size_t(z) * size + x) * 4, 1.f - hit.t, hit.normal, hit.id);
            }
        }
    };

    if(threads == 0) threads = std::max(std::thread::hardware_concurrency(), 1u);
    std::atomic<int> next { 0 };
    auto work = [&]() {
        for(int a = next++; a <= angles; a = next++) slice(a);
    };
    std::vector<std::thread> workers;
    for(unsigned i = 1; i < threads; i++) workers.emplace_back(work);
    work();
    for(auto& worker : workers) worker.join();

    return out;

}

bool saveGrassRaycast(const GrassRaycast& data, const std::string& path) {

    std::ofstream os(path, std::ios::binary);
    if(!os.good()) return false;

    uint32_t size = uint32_t(data.size), angles = uint32_t(data.angles);
    os.write(file_magic, sizeof(file_magic));
    os.write(reinterpret_cast<const char*>(&size), sizeof(size));
    os.write(reinterpret_cast<const char*>(&angles), sizeof(angles));
    os.write(reinterpret_cast<const char*>(&data.key), sizeof(data.key));
    os.write(reinterpret_cast<const char*>(data.oblique.data()), std::streamsize(data.oblique.size()));
    os.write(reinterpret_cast<const char*>(data.top.data()), std::streamsize(data.top.size()));
    return os.good();

}

bool loadGrassRaycast(GrassRaycast& data, const std::string& path) {

    std::ifstream is(path, std::ios::binary);
    if(!is.good()) return false;

    char magic[sizeof(file_magic)];
    uint32_t size = 0, angles = 0;
    uint64_t key = 0;
    is.read(magic, sizeof(magic));
    is.read(reinterpret_cast<char*>(&size), sizeof(size));
    is.read(reinterpret_cast<char*>(&angles), sizeof(angles));
    is.read(reinterpret_cast<char*>(&key), sizeof(key));
    if(!is.good() || std::memcmp(magic, file_magic, sizeof(magic)) != 0) return false;
    if(size == 0 || size > 4096 || angles == 0 || angles > 4096) return false;

    GrassRaycast read;
    read.size = int(size);
    read.angles = int(angles);
    read.key = key;
    read.oblique.resize(size_t(angles) * size * size * 4);
    read.top.resize(size_t(size) * size * 4);
    is.read(reinterpret_cast<char*>(read.oblique.data()), std::streamsize(read.oblique.size()));
    is.read(reinterpret_cast<char*>(read.top.data()), std::streamsize(read.top.size()));
    if(!is.good()) return false;

    data = std::move(read);
    return true;

}

std::vector<uint8_t> rgbaMipChain(const std::vector<uint8_t>& image, int width, int height, int depth, int& levels) {

    std::vector<uint8_t> chain = image;
    size_t source = 0;
    int made = 1;

    while(made < levels && (width > 1 || height > 1 || depth > 1)) {

        int w = std::max(width / 2, 1), h = std::max(height / 2, 1), d = std::max(depth / 2, 1);
        int sx = width > 1 ? 2 : 1, sy = height > 1 ? 2 : 1, sz = depth > 1 ? 2 : 1;
        size_t target = chain.size();
        chain.resize(target + size_t(w) * h * d * 4);

        for(int z = 0; z < d; z++) {
            for(int y = 0; y < h; y++) {
                for(int x = 0; x < w; x++) {
                    int sum[4] = {0, 0, 0, 0};
                    for(int k = 0; k < sz; k++) {
                        for(int j = 0; j < sy; j++) {
                            for(int i = 0; i < sx; i++) {
                                const uint8_t* texel = &chain[source + ((size_t(z * sz + k) * height + (y * sy + j)) * width + (x * sx + i)) * 4];
                                for(int c = 0; c < 4; c++) sum[c] += texel[c];
                            }
                        }
                    }
                    int count = sx * sy * sz;
                    uint8_t* texel = &chain[target + ((size_t(z) * h + y) * w + x) * 4];
                    for(int c = 0; c < 4; c++) texel[c] = uint8_t((sum[c] + count / 2) / count);
                }
            }
        }

        source = target;
        width = w;
        height = h;
        depth = d;
        made++;

    }

    levels = made;
    return chain;

}

glm::vec4 sampleGrassTop(const GrassRaycast& data, glm::vec2 q) {

    const int n = data.size;
    float fx = q.x * n - 0.5f, fz = q.y * n - 0.5f;
    int x0 = int(std::floor(fx)), z0 = int(std::floor(fz));
    float tx = fx - x0, tz = fz - z0;

    auto texel = [&](int x, int z) {
        return decode(&data.top[(size_t(wrap(z, n)) * n + wrap(x, n)) * 4]);
    };
    return glm::mix(glm::mix(texel(x0, z0), texel(x0 + 1, z0), tx), glm::mix(texel(x0, z0 + 1), texel(x0 + 1, z0 + 1), tx), tz);

}

glm::vec4 sampleGrassOblique(const GrassRaycast& data, glm::vec2 q, float azimuth) {

    const int n = data.size, angles = data.angles;
    float fx = q.x * n - 0.5f, fz = q.y * n - 0.5f, fa = azimuth / two_pi * angles - 0.5f;
    int x0 = int(std::floor(fx)), z0 = int(std::floor(fz)), a0 = int(std::floor(fa));
    float tx = fx - x0, tz = fz - z0, ta = fa - a0;

    auto slice = [&](int a) {
        const uint8_t* texels = &data.oblique[size_t(wrap(a, angles)) * n * n * 4];
        auto texel = [&](int x, int z) {
            return decode(&texels[(size_t(wrap(z, n)) * n + wrap(x, n)) * 4]);
        };
        return glm::mix(glm::mix(texel(x0, z0), texel(x0 + 1, z0), tx), glm::mix(texel(x0, z0 + 1), texel(x0 + 1, z0 + 1), tx), tz);
    };
    return glm::mix(slice(a0), slice(a0 + 1), ta);

}

GrassLookup lookupGrass(const GrassRaycast& data, glm::vec2 q_top, glm::vec2 along, float reach, float top_start) {

    // Everything happens around the point where the view ray crosses the middle of the grass : the baked
    // ray through that point, from the same side, says how far before or after it the first blade is
    reach = std::clamp(reach, 1e-3f, grass_max_reach);
    glm::vec2 q = q_top + along * (0.5f * reach);
    glm::vec4 oblique = sampleGrassOblique(data, q, std::atan2(along.y, along.x));

    // The view ray is over that place higher or lower than the baked ray was (blades are mostly vertical :
    // it meets the same blade there). At 45 degrees this is the baked hit itself.
    float moved = std::clamp(0.5f - (0.5f - oblique.r) / reach, 0.f, 1.f);

    // Normals are blended as they are stored, like the sampler and the shader do
    glm::vec2 normal(oblique.g, oblique.b);

    GrassLookup lookup;
    lookup.baked = oblique.r;
    lookup.height = moved;
    lookup.id = oblique.a;

    if(reach < 1.f) {
        // Between 45 degrees and the vertical : towards the top down ray of the same point
        glm::vec4 top = sampleGrassTop(data, q);
        float steep = (std::atan2(1.f, reach) - quarter_pi) / quarter_pi;
        float weight = smoothstep(std::min(top_start, 0.999f), 1.f, steep);
        lookup.baked = glm::mix(oblique.r, top.r, weight);
        lookup.height = glm::mix(moved, top.r, weight);
        lookup.id = glm::mix(oblique.a, top.a, weight);
        normal = glm::mix(normal, glm::vec2(top.g, top.b), weight);
    }
    lookup.normal = decodeGrassNormal(normal);
    return lookup;

}

}
