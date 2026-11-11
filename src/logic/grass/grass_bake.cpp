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
const float elevation_step = 0.39269908170f;    // 22.5 degrees

// Horizontal rays that meet nothing on their way go on this many times further for a blade to show
// (the others end on the ground)
const float flat_patience = 4.f;

// Bump when the baker or the meaning of the texels changes : cached files become stale
const uint32_t bake_version = 4;
const char file_magic[8] = {'D', 'Y', 'G', 'R', 'A', 'S', '3', '\0'};

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

float grassElevation(int elevation) {
    return elevation * elevation_step;
}

float grassReach(int elevation) {
    return elevation <= 0 ? grass_max_reach : 1.f / std::tan(grassElevation(elevation));
}

int GrassRaycast::slices() const {
    return firstSlice(grass_elevations);
}

int GrassRaycast::firstSlice(int elevation) const {
    int first = 0;
    for(int e = 0; e < elevation && e < grass_elevations; e++) first += angles[e];
    return first;
}

float GrassRaycast::lowElevation() const {
    return std::atan2(std::max(low_from - low_to, 0.f), grass_max_reach);
}

float GrassRaycast::height(int elevation, float r) const {
    return elevation <= 0 ? glm::mix(low_to, low_from, r) : r;
}

GrassTracer::GrassTracer(GrassModel model, int cells_) : tile(std::move(model)), cells(std::max(cells_, 1)), grid(size_t(cells) * cells),
        tops(size_t(cells) * cells, 0.f) {

    for(uint32_t i = 0; i + 2 < tile.indices.size(); i += 3) {
        glm::vec3 a = tile.vertices[tile.indices[i]], b = tile.vertices[tile.indices[i + 1]], c = tile.vertices[tile.indices[i + 2]];
        int x0 = int(std::floor(std::min({a.x, b.x, c.x}) * cells)), x1 = int(std::floor(std::max({a.x, b.x, c.x}) * cells));
        int z0 = int(std::floor(std::min({a.z, b.z, c.z}) * cells)), z1 = int(std::floor(std::max({a.z, b.z, c.z}) * cells));
        float top = std::max({a.y, b.y, c.y});
        for(int gz = z0; gz <= z1; gz++) {
            for(int gx = x0; gx <= x1; gx++) {
                // The copy of the tile that brings this part of the triangle back in the tile
                int tx = floorDiv(gx, cells), tz = floorDiv(gz, cells);
                size_t cell = size_t(gz - tz * cells) * cells + (gx - tx * cells);
                grid[cell].push_back({i, int16_t(-tx), int16_t(-tz)});
                tops[cell] = std::max(tops[cell], top);
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
            hit.colour = grass_root_colour;
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
        size_t cell = size_t(iz - tz * cells) * cells + (ix - tx * cells);

        // Nothing to meet in a cell whose blades end under the ray (under where it leaves the cell when
        // it comes down)
        bool over = o.y + d.y * (d.y < 0.f ? exit : t0) > tops[cell] + 1e-4f;

        for(const Entry& entry : grid[cell]) {
            if(over) break;
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
    hit.part = static_cast<GrassPart>(tile.parts[tile.indices[blade]]);
    hit.colour = tile.colours[tile.indices[blade]] * (1.f - blade_u - blade_v)
        + tile.colours[tile.indices[blade + 1]] * blade_u + tile.colours[tile.indices[blade + 2]] * blade_v;
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
    for(int angles : params.angles) add(uint32_t(angles));
    add_float(params.low_from);
    add_float(params.low_to);
    add_float(grass_max_reach);
    const GrassModelParams& m = params.model;
    add(m.seed);
    add(uint32_t(m.tufts));
    add(uint32_t(m.blades_min));
    add(uint32_t(m.blades_max));
    add(uint32_t(m.segments));
    for(float value : {m.height_min, m.height_max, m.max_height, m.width, m.lean, m.fold}) add_float(value);
    add(static_cast<uint32_t>(m.kind));
    return hash;

}

GrassRaycast bakeGrass(const GrassBakeParams& params, unsigned threads) {
    return bakeGrassModel(generateGrassTile(params.model), params, grassBakeKey(params), threads);
}

GrassRaycast bakeGrassModel(GrassModel model, const GrassBakeParams& params, uint64_t key, unsigned threads, int cells) {

    const int size = std::max(params.size, 1);

    GrassRaycast out;
    out.size = size;
    for(int e = 0; e < grass_elevations; e++) out.angles[e] = std::max(params.angles[e], 1);
    out.low_from = std::clamp(params.low_from, 0.f, 1.f);
    out.low_to = std::clamp(params.low_to, 0.f, out.low_from);
    out.key = key;
    const int slices = out.slices();
    out.oblique.resize(size_t(slices) * size * size * 4);
    out.top.resize(size_t(size) * size * 4);
    out.oblique_colour.resize(out.oblique.size(), 255);
    out.top_colour.resize(out.top.size(), 255);

    GrassTracer tracer(std::move(model), cells);

    // Slice `slices` is the top down one. A ray is at its start at t = 0, over its texel at t = 0.5 and
    // at its end at t = 1 : from the top of the grass to the ground, but for the lowest rays
    auto slice = [&](int index) {
        bool top = index == slices;
        int elevation = 0;
        while(!top && index >= out.firstSlice(elevation + 1)) elevation++;
        bool low = !top && elevation == 0;

        float reach = top ? 0.f : grassReach(elevation);
        float from = low ? out.low_from : 1.f, fall = low ? out.low_from - out.low_to : 1.f;
        float azimuth = top ? 0.f : (index - out.firstSlice(elevation) + 0.5f) / out.angles[elevation] * two_pi;
        glm::vec2 way = glm::vec2(std::cos(azimuth), std::sin(azimuth)) * reach;
        glm::vec3 direction(way.x, -fall, way.y);
        uint8_t* texels = top ? out.top.data() : out.oblique.data() + size_t(index) * size * size * 4;
        uint8_t* colours = top ? out.top_colour.data() : out.oblique_colour.data() + size_t(index) * size * size * 4;

        for(int z = 0; z < size; z++) {
            for(int x = 0; x < size; x++) {
                glm::vec2 q((x + 0.5f) / size, (z + 0.5f) / size);
                glm::vec3 origin(q.x - 0.5f * way.x, from, q.y - 0.5f * way.y);
                // Only a blade stops a horizontal ray : without one it is the ground at the end of its way
                GrassTracer::Hit hit;
                hit.t = 1.f;
                tracer.trace(origin, direction, hit, fall > 0.f ? 1e4f : flat_patience);
                encode(texels + (size_t(z) * size + x) * 4, 1.f - hit.t, hit.normal, hit.id);
                encodeGrassColour(hit.colour, colours + (size_t(z) * size + x) * 4);
            }
        }
    };

    if(threads == 0) threads = std::max(std::thread::hardware_concurrency(), 1u);
    std::atomic<int> next { 0 };
    auto work = [&]() {
        for(int index = next++; index <= slices; index = next++) slice(index);
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

    uint32_t size = uint32_t(data.size), elevations = uint32_t(grass_elevations);
    os.write(file_magic, sizeof(file_magic));
    os.write(reinterpret_cast<const char*>(&size), sizeof(size));
    os.write(reinterpret_cast<const char*>(&elevations), sizeof(elevations));
    for(int count : data.angles) {
        uint32_t angles = uint32_t(count);
        os.write(reinterpret_cast<const char*>(&angles), sizeof(angles));
    }
    os.write(reinterpret_cast<const char*>(&data.low_from), sizeof(data.low_from));
    os.write(reinterpret_cast<const char*>(&data.low_to), sizeof(data.low_to));
    os.write(reinterpret_cast<const char*>(&data.key), sizeof(data.key));
    os.write(reinterpret_cast<const char*>(data.oblique.data()), std::streamsize(data.oblique.size()));
    os.write(reinterpret_cast<const char*>(data.top.data()), std::streamsize(data.top.size()));
    os.write(reinterpret_cast<const char*>(data.oblique_colour.data()), std::streamsize(data.oblique_colour.size()));
    os.write(reinterpret_cast<const char*>(data.top_colour.data()), std::streamsize(data.top_colour.size()));
    return os.good();

}

bool loadGrassRaycast(GrassRaycast& data, const std::string& path) {

    std::ifstream is(path, std::ios::binary);
    if(!is.good()) return false;

    char magic[sizeof(file_magic)];
    uint32_t size = 0, elevations = 0, angles[grass_elevations] = {};
    float low[2] = {};
    uint64_t key = 0;
    is.read(magic, sizeof(magic));
    is.read(reinterpret_cast<char*>(&size), sizeof(size));
    is.read(reinterpret_cast<char*>(&elevations), sizeof(elevations));
    if(!is.good() || std::memcmp(magic, file_magic, sizeof(magic)) != 0 || elevations != uint32_t(grass_elevations)) return false;
    is.read(reinterpret_cast<char*>(angles), sizeof(angles));
    is.read(reinterpret_cast<char*>(low), sizeof(low));
    is.read(reinterpret_cast<char*>(&key), sizeof(key));
    if(!is.good() || size == 0 || size > 4096) return false;
    if(!(low[0] >= 0.f && low[0] <= 1.f && low[1] >= 0.f && low[1] <= low[0])) return false;

    GrassRaycast read;
    read.size = int(size);
    read.low_from = low[0];
    read.low_to = low[1];
    for(int e = 0; e < grass_elevations; e++) {
        if(angles[e] == 0 || angles[e] > 4096) return false;
        read.angles[e] = int(angles[e]);
    }
    read.key = key;
    read.oblique.resize(size_t(read.slices()) * size * size * 4);
    read.top.resize(size_t(size) * size * 4);
    is.read(reinterpret_cast<char*>(read.oblique.data()), std::streamsize(read.oblique.size()));
    is.read(reinterpret_cast<char*>(read.top.data()), std::streamsize(read.top.size()));
    read.oblique_colour.resize(read.oblique.size());
    read.top_colour.resize(read.top.size());
    is.read(reinterpret_cast<char*>(read.oblique_colour.data()), std::streamsize(read.oblique_colour.size()));
    is.read(reinterpret_cast<char*>(read.top_colour.data()), std::streamsize(read.top_colour.size()));
    if(!is.good()) return false;

    data = std::move(read);
    return true;

}

std::vector<uint8_t> rgbaMipChain(const std::vector<uint8_t>& image, int width, int height, int depth, int& levels, bool layers) {

    std::vector<uint8_t> chain = image;
    size_t source = 0;
    int made = 1;

    while(made < levels && (width > 1 || height > 1 || (depth > 1 && !layers))) {

        int w = std::max(width / 2, 1), h = std::max(height / 2, 1), d = layers ? depth : std::max(depth / 2, 1);
        int sx = width > 1 ? 2 : 1, sy = height > 1 ? 2 : 1, sz = depth > 1 && !layers ? 2 : 1;
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

glm::vec4 sampleGrassOblique(const GrassRaycast& data, int elevation, glm::vec2 q, float azimuth) {

    const int n = data.size, angles = data.angles[elevation];
    float fx = q.x * n - 0.5f, fz = q.y * n - 0.5f, fa = azimuth / two_pi * angles - 0.5f;
    int x0 = int(std::floor(fx)), z0 = int(std::floor(fz)), a0 = int(std::floor(fa));
    float tx = fx - x0, tz = fz - z0, ta = fa - a0;

    auto slice = [&](int a) {
        const uint8_t* texels = &data.oblique[size_t(data.firstSlice(elevation) + wrap(a, angles)) * n * n * 4];
        auto texel = [&](int x, int z) {
            return decode(&texels[(size_t(wrap(z, n)) * n + wrap(x, n)) * 4]);
        };
        return glm::mix(glm::mix(texel(x0, z0), texel(x0 + 1, z0), tx), glm::mix(texel(x0, z0 + 1), texel(x0 + 1, z0 + 1), tx), tz);
    };
    return glm::mix(slice(a0), slice(a0 + 1), ta);

}

void encodeGrassColour(glm::vec3 colour, uint8_t* texel) {
    for(int c = 0; c < 3; c++) {
        float linear = std::clamp(colour[c], 0.f, 1.f);
        float encoded = linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
        texel[c] = static_cast<uint8_t>(std::lround(encoded * 255.f));
    }
}

glm::vec3 decodeGrassColour(const uint8_t* texel) {
    glm::vec3 colour;
    for(int c = 0; c < 3; c++) {
        float encoded = texel[c] / 255.f;
        colour[c] = encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    }
    return colour;
}

glm::vec3 sampleGrassTopColour(const GrassRaycast& data, glm::vec2 q) {

    const int n = data.size;
    float fx = q.x * n - 0.5f, fz = q.y * n - 0.5f;
    int x0 = int(std::floor(fx)), z0 = int(std::floor(fz));
    float tx = fx - x0, tz = fz - z0;
    auto texel = [&](int x, int z) {
        return decodeGrassColour(&data.top_colour[(size_t(wrap(z, n)) * n + wrap(x, n)) * 4]);
    };
    return glm::mix(glm::mix(texel(x0, z0), texel(x0 + 1, z0), tx), glm::mix(texel(x0, z0 + 1), texel(x0 + 1, z0 + 1), tx), tz);

}

glm::vec3 sampleGrassObliqueColour(const GrassRaycast& data, int elevation, glm::vec2 q, float azimuth) {

    const int n = data.size, angles = data.angles[elevation];
    float fx = q.x * n - 0.5f, fz = q.y * n - 0.5f, fa = azimuth / two_pi * angles - 0.5f;
    int x0 = int(std::floor(fx)), z0 = int(std::floor(fz)), a0 = int(std::floor(fa));
    float tx = fx - x0, tz = fz - z0, ta = fa - a0;

    auto slice = [&](int a) {
        const uint8_t* texels = &data.oblique_colour[size_t(data.firstSlice(elevation) + wrap(a, angles)) * n * n * 4];
        auto texel = [&](int x, int z) {
            return decodeGrassColour(&texels[(size_t(wrap(z, n)) * n + wrap(x, n)) * 4]);
        };
        return glm::mix(glm::mix(texel(x0, z0), texel(x0 + 1, z0), tx), glm::mix(texel(x0, z0 + 1), texel(x0 + 1, z0 + 1), tx), tz);
    };
    return glm::mix(slice(a0), slice(a0 + 1), ta);

}

float grassSeeThrough(float sink, float falling, float flat_length) {
    if(falling <= 1e-5f) return std::numeric_limits<float>::infinity();
    sink = std::max(sink, 0.f);
    return sink * sink / (2.f * falling * std::max(flat_length * flat_length, 1e-4f));
}

GrassLookup lookupGrass(const GrassRaycast& data, glm::vec2 q_top, glm::vec2 along, float reach, float top_start) {

    // The two baked elevations around the ray's, blended along the tiles they cross (their cotangent : a
    // view carried to a lower ray stays closer to it than to a higher one). The lowest rays answer for
    // everything under them, they would cross more than grass_max_reach tiles on their way to the ground
    reach = std::max(reach, 0.f);
    int lower = grass_elevations - 1;
    while(lower > 0 && reach >= grassReach(lower)) lower--;
    float from_reach = lower > 0 ? grassReach(lower) : std::min(1.f / std::tan(std::max(data.lowElevation(), 1e-3f)), 1e3f);
    float to_reach = lower + 1 < grass_elevations ? grassReach(lower + 1) : 0.f;
    float weight = std::clamp((from_reach - reach) / (from_reach - to_reach), 0.f, 1.f);

    // Everything happens around the point where the view ray crosses the middle of the grass : the baked
    // rays through that point, from the same side, say how far before or after it the first blade is
    reach = std::clamp(reach, 1e-3f, grass_max_reach);
    glm::vec2 q = q_top + along * (0.5f * reach);
    float azimuth = std::atan2(along.y, along.x);

    // What the rays of one elevation say : the texel, the height of the hit on the view ray and on
    // the baked ray
    struct View {
        glm::vec4 texel;
        float height, baked;
    };
    auto oblique = [&](int elevation) {
        View view;
        view.texel = sampleGrassOblique(data, elevation, q, azimuth);
        // The baked ray met a blade this many tiles after the middle. The view ray is over that place
        // higher or lower than the baked ray was (blades are mostly vertical : it meets the same blade
        // there). At the baked elevation this is the baked hit itself.
        float ahead = (0.5f - view.texel.r) * grassReach(elevation);
        view.height = std::clamp(0.5f - ahead / reach, 0.f, 1.f);
        view.baked = data.height(elevation, view.texel.r);
        return view;
    };

    View from = oblique(lower), to;
    if(lower + 1 < grass_elevations) {
        to = oblique(lower + 1);
    } else {
        // Over the last baked elevation : towards the top down ray of the same point
        to.texel = sampleGrassTop(data, q);
        to.height = to.baked = to.texel.r;
        weight = smoothstep(std::min(top_start, 0.999f), 1.f, weight);
    }

    // Normals are blended as they are stored, like the shader does
    glm::vec4 texel = glm::mix(from.texel, to.texel, weight);

    GrassLookup lookup;
    lookup.baked = glm::mix(from.baked, to.baked, weight);
    lookup.height = glm::mix(from.height, to.height, weight);
    lookup.normal = decodeGrassNormal(glm::vec2(texel.g, texel.b));
    lookup.id = texel.a;
    lookup.colour = glm::mix(sampleGrassObliqueColour(data, lower, q, azimuth), lower + 1 < grass_elevations
        ? sampleGrassObliqueColour(data, lower + 1, q, azimuth) : sampleGrassTopColour(data, q), weight);
    return lookup;

}

}
