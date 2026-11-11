#include "grass_model.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace dy {

namespace {

// PCG hash : the same blades on every platform (std distributions are implementation defined)
struct Rng {
    uint32_t state;

    explicit Rng(uint32_t seed) : state(seed * 747796405u + 2891336453u) {}

    float next() {
        state = state * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        word = (word >> 22u) ^ word;
        return (word >> 8) * (1.f / 16777216.f);
    }

    float range(float a, float b) { return a + (b - a) * next(); }
};

const float pi = 3.14159265359f;
const float two_pi = 6.28318530718f;
const glm::vec3 up(0.f, 1.f, 0.f);

const GrassKindInfo kinds[grass_kinds] = {
    {"meadow", "MEADOW GRASS", 1.f, glm::vec3(1.f), glm::vec3(0.7f, 0.65f, 0.4f), false, 0.3f},
    {"fescue", "FINE FESCUE", 0.5f, glm::vec3(1.f), glm::vec3(0.7f, 0.65f, 0.4f), true, 0.1f},
    {"cocksfoot", "BROAD LEAF GRASS", 1.f, glm::vec3(1.f), glm::vec3(0.7f, 0.65f, 0.4f), false, 0.f},
    {"ryegrass", "RYEGRASS", 0.6f, glm::vec3(1.f), glm::vec3(0.62f, 0.66f, 0.3f), false, 0.1f},
    {"timothy", "TIMOTHY", 0.6f, glm::vec3(1.f), glm::vec3(0.52f, 0.6f, 0.38f), false, 0.f},
    {"oat", "OAT GRASS", 0.8f, glm::vec3(1.f), glm::vec3(0.8f, 0.68f, 0.38f), false, 0.f},
    {"clover", "CLOVER", 0.4f, glm::vec3(0.96f, 0.93f, 0.9f), glm::vec3(0.7f, 0.65f, 0.4f), true, 0.1f},
    {"clover_flower", "CLOVER IN FLOWER", 0.4f, glm::vec3(0.96f, 0.93f, 0.9f), glm::vec3(0.7f, 0.65f, 0.4f), true, 0.05f},
    {"plantain", "PLANTAIN", 0.6f, glm::vec3(1.f), glm::vec3(0.4f, 0.3f, 0.2f), true, 0.f},
    {"dandelion", "DANDELION", 0.6f, glm::vec3(1.f, 0.8f, 0.08f), glm::vec3(0.7f, 0.65f, 0.4f), true, 0.05f},
};

glm::vec2 heading(float azimuth) { return glm::vec2(std::cos(azimuth), std::sin(azimuth)); }
glm::vec3 flat(glm::vec2 v) { return glm::vec3(v.x, 0.f, v.y); }

glm::vec3 unit(glm::vec3 v, glm::vec3 otherwise = up) {
    float length = glm::length(v);
    return length > 1e-6f ? v / length : otherwise;
}

// A leaf that rises and bends out, its tip `droop` of its height under where it would be
struct Arch {
    glm::vec2 root, out;
    float height, reach, droop;

    glm::vec3 operator()(float s) const {
        glm::vec2 p = root + out * reach * (0.25f * s + 0.75f * s * s);
        return glm::vec3(p.x, height * (s * (2.f - s) - droop * s * s * s * s), p.y);
    }
};

// A stem, leaning more and more
struct Stalk {
    glm::vec2 root, out;
    float height, reach;

    glm::vec3 operator()(float s) const {
        glm::vec2 p = root + out * reach * s * s;
        return glm::vec3(p.x, height * s, p.y);
    }
};

template<class Spine>
glm::vec3 tangent(const Spine& spine, float s) {
    return unit(spine(std::min(s + 0.02f, 1.f)) - spine(std::max(s - 0.02f, 0.f)));
}

// The pieces plants are made of, in metres
struct Builder {
    GrassModel& model;

    uint32_t begin() const { return static_cast<uint32_t>(model.vertices.size()); }

    void push(glm::vec3 v) {
        v.y = std::max(v.y, 0.f);
        model.vertices.push_back(v);
    }

    void triangle(uint32_t a, uint32_t b, uint32_t c) {
        model.indices.push_back(a);
        model.indices.push_back(b);
        model.indices.push_back(c);
    }

    void end(float id, GrassPart part, bool count = true) {
        model.ids.resize(model.vertices.size(), id);
        model.parts.resize(model.vertices.size(), static_cast<uint8_t>(part));
        if(count) model.blades++;
    }

    // A blade folded in a V along its spine, `side` is across it
    template<class Spine, class Width>
    void ribbon(const Spine& spine, const Width& half_width, glm::vec3 side, float fold, int levels, float id, GrassPart part) {
        uint32_t base = begin();
        for(int level = 0; level < levels; level++) {
            float s = float(level) / levels;
            float w = half_width(s);
            glm::vec3 c = spine(s);
            glm::vec3 face = unit(glm::cross(side, tangent(spine, s)));
            push(c - side * w);
            push(c - face * (fold * w));
            push(c + side * w);
        }
        uint32_t tip = begin();
        push(spine(1.f));
        end(id, part);

        for(int level = 0; level + 1 < levels; level++) {
            uint32_t l0 = base + level * 3, m0 = l0 + 1, r0 = l0 + 2;
            uint32_t l1 = l0 + 3, m1 = l1 + 1, r1 = l1 + 2;
            triangle(l0, m0, l1);
            triangle(m0, m1, l1);
            triangle(m0, r0, m1);
            triangle(r0, r1, m1);
        }
        uint32_t l = base + (levels - 1) * 3;
        triangle(l, l + 1, tip);
        triangle(l + 1, l + 2, tip);
    }

    // A stem : it has to be met from every side
    template<class Spine, class Radius>
    void tube(const Spine& spine, const Radius& radius, int levels, int sides, float id, GrassPart part) {
        uint32_t base = begin();
        glm::vec3 u(1.f, 0.f, 0.f);
        for(int level = 0; level <= levels; level++) {
            float s = float(level) / levels;
            glm::vec3 c = spine(s), t = tangent(spine, s);
            u = unit(u - t * glm::dot(u, t), unit(glm::cross(t, glm::vec3(0.f, 0.f, 1.f)), glm::vec3(1.f, 0.f, 0.f)));
            glm::vec3 v = glm::cross(t, u);
            float r = radius(s);
            for(int k = 0; k < sides; k++) {
                float a = two_pi * k / sides;
                push(c + (u * std::cos(a) + v * std::sin(a)) * r);
            }
        }
        end(id, part);
        for(int level = 0; level < levels; level++) {
            for(int k = 0; k < sides; k++) {
                uint32_t i0 = base + level * sides + k, i1 = base + level * sides + (k + 1) % sides;
                triangle(i0, i1, i0 + sides);
                triangle(i1, i1 + sides, i0 + sides);
            }
        }
    }

    // A spindle from a to b : spikelets (one ring), heads (power under 1 : a cylinder with round ends)
    void blob(glm::vec3 a, glm::vec3 b, float radius, int sides, int rings, float power, float flatten, float id, GrassPart part,
            bool count = true) {
        glm::vec3 t = unit(b - a);
        glm::vec3 u = unit(glm::cross(t, std::abs(t.y) < 0.9f ? up : glm::vec3(1.f, 0.f, 0.f)));
        glm::vec3 v = glm::cross(t, u);
        uint32_t base = begin();
        push(a);
        for(int ring = 1; ring <= rings; ring++) {
            float s = float(ring) / (rings + 1);
            float r = radius * std::pow(std::sin(pi * s), power);
            glm::vec3 c = glm::mix(a, b, s);
            for(int k = 0; k < sides; k++) {
                float angle = two_pi * k / sides;
                push(c + u * (std::cos(angle) * r) + v * (std::sin(angle) * r * flatten));
            }
        }
        uint32_t tip = begin();
        push(b);
        end(id, part, count);
        for(int k = 0; k < sides; k++) {
            uint32_t n = (k + 1) % sides;
            triangle(base, base + 1 + n, base + 1 + k);
            uint32_t last = base + 1 + (rings - 1) * sides;
            triangle(tip, last + k, last + n);
            for(int ring = 0; ring + 1 < rings; ring++) {
                uint32_t r0 = base + 1 + ring * sides, r1 = r0 + sides;
                triangle(r0 + k, r0 + n, r1 + k);
                triangle(r0 + n, r1 + n, r1 + k);
            }
        }
    }

    // A round leaf, wider towards its end, a little hollow
    void leaflet(glm::vec3 from, glm::vec3 axis, glm::vec3 across, float length, float width, float cup, int rim, float id, GrassPart part) {
        glm::vec3 normal = unit(glm::cross(across, axis));
        if(normal.y < 0.f) normal = -normal;
        uint32_t base = begin();
        push(from + axis * (0.55f * length) - normal * (cup * width));
        for(int i = 0; i < rim; i++) {
            float angle = two_pi * i / rim;
            float u = 0.5f * (1.f - std::cos(angle));
            float v = 0.5f * std::sin(angle) * (0.5f + 0.75f * u);
            push(from + axis * (u * length) + across * (v * width));
        }
        end(id, part, false);
        for(int i = 0; i < rim; i++) triangle(base, base + 1 + i, base + 1 + (i + 1) % rim);
    }
};

// n x n plants on a jittered grid : plant(center)
template<class Plant>
void scatter(Rng& rng, int n, float tile, const Plant& plant) {
    for(int tz = 0; tz < n; tz++) {
        for(int tx = 0; tx < n; tx++) {
            float x = (tx + rng.range(0.15f, 0.85f)) / n;
            float z = (tz + rng.range(0.15f, 0.85f)) / n;
            plant(glm::vec2(x, z) * tile);
        }
    }
}

// Grass leaves fanning out from a point
void tuft(Builder& b, Rng& rng, glm::vec2 center, int count, float spread, float height_min, float height_max, float width,
        float lean, float droop, int levels, float fold) {
    float start = rng.next() * two_pi;
    for(int i = 0; i < count; i++) {
        float azimuth = start + (i + rng.range(-0.3f, 0.3f)) * two_pi / count;
        glm::vec2 out = heading(azimuth);
        Arch spine { center, out, 0.f, 0.f, 0.f };
        spine.root += out * spread * rng.range(0.2f, 1.f);
        spine.height = rng.range(height_min, height_max);
        spine.reach = lean * rng.range(0.3f, 1.f) * spine.height;
        spine.droop = droop * rng.range(0.4f, 1.f);
        float half_width = 0.5f * width * rng.range(0.7f, 1.3f);
        float twist = azimuth + 0.5f * pi + rng.range(-0.5f, 0.5f);
        b.ribbon(spine, [half_width](float s) { return half_width * std::pow(1.f - s, 0.6f); }, flat(heading(twist)), fold, levels,
            rng.next(), GrassPart::Blade);
    }
}

// The tuft at the foot of a grass that bears a stem
void footTuft(Builder& b, Rng& rng, glm::vec2 center, float height_min, float height_max, float width) {
    int count = 4 + int(rng.next() * 3.f);
    float height = rng.range(height_min, height_max);
    tuft(b, rng, center, count, 0.01f, 0.6f * height, height, width, 0.45f, 0.5f, 5, 0.4f);
}

Stalk stalk(Rng& rng, glm::vec2 center, float height_min, float height_max, float reach_min, float reach_max) {
    Stalk s { center, glm::vec2(0.f), 0.f, 0.f };
    s.out = heading(rng.next() * two_pi);
    s.root += s.out * 0.004f;
    s.height = rng.range(height_min, height_max);
    s.reach = rng.range(reach_min, reach_max);
    return s;
}

void fescue(Builder& b, Rng& rng, float tile) {
    scatter(rng, 11, tile, [&](glm::vec2 center) {
        int count = 9 + int(rng.next() * 6.f);
        float height = 0.3f * rng.range(0.5f, 1.f);
        tuft(b, rng, center, count, 0.012f, 0.5f * height, height, 0.0035f, 0.5f, 0.45f, 6, 0.5f);
    });
}

void cocksfoot(Builder& b, Rng& rng, float tile) {
    scatter(rng, 5, tile, [&](glm::vec2 center) {
        int count = 9 + int(rng.next() * 5.f);
        float height = 0.6f * rng.range(0.6f, 1.f);
        tuft(b, rng, center, count, 0.04f, 0.5f * height, height, 0.024f, 0.7f, 0.9f, 8, 0.3f);
    });
}

// Spikelets on either side of the stem, in one plane, close to it
void ryegrass(Builder& b, Rng& rng, float tile) {
    scatter(rng, 7, tile, [&](glm::vec2 center) {
        footTuft(b, rng, center, 0.15f, 0.35f, 0.005f);
        if(rng.next() > 0.6f) return;
        Stalk stem = stalk(rng, center, 0.5f, 0.7f, 0.02f, 0.1f);
        float id = rng.next();
        b.tube(stem, [](float) { return 0.002f; }, 8, 3, id, GrassPart::Stem);
        glm::vec3 plane = flat(heading(rng.next() * two_pi));
        int count = 14 + int(rng.next() * 5.f);
        for(int j = 0; j < count; j++) {
            float s = 0.62f + 0.37f * j / (count - 1);
            float sign = (j & 1) ? 1.f : -1.f;
            glm::vec3 from = stem(s) + plane * (sign * 0.002f);
            glm::vec3 along = unit(tangent(stem, s) * 0.9f + plane * (sign * 0.42f));
            float length = 0.016f * (1.f - 0.3f * j / count);
            b.blob(from, from + along * length, 0.0032f, 4, 1, 1.f, 0.6f, id, GrassPart::Seed, false);
        }
    });
}

void timothy(Builder& b, Rng& rng, float tile) {
    scatter(rng, 6, tile, [&](glm::vec2 center) {
        footTuft(b, rng, center, 0.2f, 0.45f, 0.007f);
        if(rng.next() > 0.6f) return;
        Stalk stem = stalk(rng, center, 0.5f, 0.75f, 0.01f, 0.06f);
        float id = rng.next();
        b.tube(stem, [](float) { return 0.0022f; }, 8, 3, id, GrassPart::Stem);
        glm::vec3 t = tangent(stem, 1.f);
        float length = rng.range(0.05f, 0.09f);
        b.blob(stem(1.f) - t * 0.004f, stem(1.f) + t * length, 0.0058f, 6, 5, 0.35f, 1.f, id, GrassPart::Seed, false);
    });
}

// Whorls of thin branches, a spikelet hanging at the end of each
void oat(Builder& b, Rng& rng, float tile) {
    scatter(rng, 7, tile, [&](glm::vec2 center) {
        footTuft(b, rng, center, 0.2f, 0.5f, 0.008f);
        if(rng.next() > 0.35f) return;
        Stalk stem = stalk(rng, center, 0.7f, 0.9f, 0.03f, 0.12f);
        float id = rng.next();
        b.tube(stem, [](float) { return 0.0024f; }, 8, 3, id, GrassPart::Stem);
        for(int node = 0; node < 6; node++) {
            glm::vec3 from = stem(0.6f + 0.07f * node);
            int branches = 2 + int(rng.next() * 2.f);
            float start = rng.next() * two_pi;
            for(int k = 0; k < branches; k++) {
                float azimuth = start + (k + rng.range(-0.15f, 0.15f)) * two_pi / branches;
                float length = rng.range(0.04f, 0.075f) * (1.f - 0.08f * node);
                glm::vec3 out = flat(heading(azimuth));
                auto branch = [=](float u) { return from + out * (length * u) + up * (length * (0.7f * u - u * u)); };
                b.tube(branch, [](float) { return 0.0018f; }, 4, 3, id, GrassPart::Stem);
                glm::vec3 hang = unit(tangent(branch, 1.f) + glm::vec3(0.f, -1.2f, 0.f));
                b.blob(branch(1.f), branch(1.f) + hang * 0.02f, 0.0038f, 4, 2, 0.8f, 0.7f, id, GrassPart::Seed, false);
            }
        }
        b.blob(stem(1.f), stem(1.f) + tangent(stem, 1.f) * 0.022f, 0.0038f, 4, 2, 0.8f, 0.7f, id, GrassPart::Seed, false);
    });
}

void cloverLeaves(Builder& b, Rng& rng, float tile) {
    scatter(rng, 13, tile, [&](glm::vec2 center) {
        Stalk stem = stalk(rng, center, 0.035f, 0.11f, 0.f, 0.f);
        stem.reach = stem.height * rng.range(0.1f, 0.6f);
        float id = rng.next();
        b.tube(stem, [](float) { return 0.0011f; }, 4, 3, id, GrassPart::Stem);
        float start = rng.next() * two_pi;
        float size = rng.range(0.02f, 0.03f);
        float tilt = rng.range(0.1f, 0.5f);
        for(int k = 0; k < 3; k++) {
            glm::vec2 h = heading(start + k * two_pi / 3.f);
            glm::vec3 axis = flat(h) * std::cos(tilt) + up * std::sin(tilt);
            b.leaflet(stem(1.f) + axis * 0.001f, axis, glm::vec3(-h.y, 0.f, h.x), size, 0.95f * size, 0.08f, 10, id, GrassPart::Leaf);
        }
    });
}

// Round heads of florets over the leaves
void cloverHeads(Builder& b, Rng& rng, float tile) {
    scatter(rng, 4, tile, [&](glm::vec2 center) {
        if(rng.next() > 0.7f) return;
        Stalk stem = stalk(rng, center, 0.12f, 0.17f, 0.01f, 0.04f);
        float id = rng.next();
        b.tube(stem, [](float) { return 0.0013f; }, 5, 3, id, GrassPart::Stem);
        float radius = rng.range(0.009f, 0.012f);
        glm::vec3 middle = stem(1.f) + up * (0.7f * radius);
        const int florets = 34;
        for(int i = 0; i < florets; i++) {
            float y = 1.f - (i + 0.5f) / florets * 1.5f;
            float r = std::sqrt(std::max(1.f - y * y, 0.f)), angle = i * 2.39996f;
            glm::vec3 d(r * std::cos(angle), y, r * std::sin(angle));
            float length = radius * rng.range(0.85f, 1.1f);
            b.blob(middle + d * (0.2f * radius), middle + d * length, 0.32f * radius, 3, 1, 1.f, 1.f, id, GrassPart::Flower, false);
        }
    });
}

void plantain(Builder& b, Rng& rng, float tile) {
    scatter(rng, 3, tile, [&](glm::vec2 center) {
        int leaves = 8 + int(rng.next() * 5.f);
        float start = rng.next() * two_pi;
        for(int i = 0; i < leaves; i++) {
            float azimuth = start + (i + rng.range(-0.3f, 0.3f)) * two_pi / leaves;
            glm::vec2 out = heading(azimuth);
            float length = rng.range(0.12f, 0.2f);
            float elevation = rng.range(0.45f, 1.1f);
            Arch spine { center + out * 0.004f, out, length * std::sin(elevation), length * std::cos(elevation), 0.f };
            spine.droop = rng.range(0.2f, 0.5f);
            float half_width = rng.range(0.011f, 0.017f);
            float twist = azimuth + 0.5f * pi + rng.range(-0.2f, 0.2f);
            // Lance shaped : narrow at the foot, widest before the middle
            auto width = [half_width](float s) {
                return half_width * (std::pow(std::sin(pi * std::pow(s, 0.75f)), 0.8f) + 0.15f * (1.f - s));
            };
            b.ribbon(spine, width, flat(heading(twist)), 0.3f, 8, rng.next(), GrassPart::Leaf);
        }
        int stems = 2 + int(rng.next() * 3.f);
        for(int i = 0; i < stems; i++) {
            Stalk stem = stalk(rng, center, 0.2f, 0.34f, 0.03f, 0.09f);
            float id = rng.next();
            b.tube(stem, [](float) { return 0.0017f; }, 6, 3, id, GrassPart::Stem);
            glm::vec3 t = tangent(stem, 1.f);
            float length = rng.range(0.02f, 0.035f);
            b.blob(stem(1.f) - t * 0.003f, stem(1.f) + t * length, 0.0045f, 6, 4, 0.5f, 1.f, id, GrassPart::Seed, false);
        }
    });
}

void dandelion(Builder& b, Rng& rng, float tile) {
    scatter(rng, 3, tile, [&](glm::vec2 center) {
        int leaves = 9 + int(rng.next() * 5.f);
        float start = rng.next() * two_pi;
        for(int i = 0; i < leaves; i++) {
            float azimuth = start + (i + rng.range(-0.3f, 0.3f)) * two_pi / leaves;
            glm::vec2 out = heading(azimuth);
            float length = rng.range(0.1f, 0.19f);
            float elevation = rng.range(0.12f, 0.6f);
            Arch spine { center + out * 0.004f, out, length * std::sin(elevation), length * std::cos(elevation), 0.f };
            spine.droop = rng.range(0.1f, 0.3f);
            float half_width = rng.range(0.013f, 0.02f);
            float twist = azimuth + 0.5f * pi + rng.range(-0.2f, 0.2f);
            // Wider towards the end, cut in teeth that point back to the foot
            auto width = [half_width](float s) {
                float tooth = 1.f - (s * 6.f - std::floor(s * 6.f));
                return half_width * (0.25f + 0.75f * s) * (1.f - 0.3f * std::pow(s, 6.f)) * (0.35f + 0.65f * tooth);
            };
            b.ribbon(spine, width, flat(heading(twist)), 0.15f, 18, rng.next(), GrassPart::Leaf);
        }
        int stems = 1 + int(rng.next() * 3.f);
        for(int i = 0; i < stems; i++) {
            Stalk stem = stalk(rng, center, 0.1f, 0.28f, 0.01f, 0.05f);
            float id = rng.next();
            b.tube(stem, [](float) { return 0.0025f; }, 6, 4, id, GrassPart::Stem);
            glm::vec3 top = stem(1.f), t = tangent(stem, 1.f);
            if(rng.next() > 0.65f) {
                // A bud
                b.blob(top - t * 0.004f, top + t * 0.018f, 0.0055f, 6, 3, 0.8f, 1.f, id, GrassPart::Stem, false);
                continue;
            }
            b.blob(top - t * 0.012f, top + t * 0.002f, 0.006f, 6, 2, 0.7f, 1.f, id, GrassPart::Stem, false);
            glm::vec3 u = unit(glm::cross(t, glm::vec3(0.f, 0.f, 1.f)), glm::vec3(1.f, 0.f, 0.f)), v = glm::cross(t, u);
            const struct { int count; float tilt, length; } rings[3] = {{18, 0.2f, 0.024f}, {14, 0.7f, 0.018f}, {8, 1.2f, 0.01f}};
            uint32_t base = b.begin();
            for(auto& ring : rings) {
                for(int k = 0; k < ring.count; k++) {
                    float angle = two_pi * (k + rng.next() * 0.5f) / ring.count;
                    glm::vec3 radial = u * std::cos(angle) + v * std::sin(angle);
                    glm::vec3 across = glm::cross(t, radial) * 0.0028f;
                    glm::vec3 from = top + radial * 0.002f;
                    b.push(from - across);
                    b.push(from + across);
                    b.push(from + (radial * std::cos(ring.tilt) + t * std::sin(ring.tilt)) * ring.length);
                }
            }
            for(uint32_t index = base; index < b.begin(); index += 3) b.triangle(index, index + 1, index + 2);
            b.end(id, GrassPart::Flower, false);
        }
    });
}

void meadow(GrassModel& model, Rng& rng, const GrassModelParams& params) {

    const int tufts = std::max(params.tufts, 1);
    const int levels = std::max(params.segments, 2);

    for(int tz = 0; tz < tufts; tz++) {
        for(int tx = 0; tx < tufts; tx++) {

            glm::vec2 center((tx + rng.range(0.1f, 0.9f)) / tufts, (tz + rng.range(0.1f, 0.9f)) / tufts);
            float tuft_height = rng.range(params.height_min, params.height_max);
            int count = params.blades_min + int(rng.next() * (params.blades_max - params.blades_min + 1));
            count = std::clamp(count, 1, std::max(params.blades_max, 1));
            float start = rng.next() * two_pi;

            for(int b = 0; b < count; b++) {

                // Blades of a tuft fan out from its center
                float azimuth = start + (b + rng.range(-0.3f, 0.3f)) * two_pi / count;
                glm::vec2 out(std::cos(azimuth), std::sin(azimuth));
                glm::vec2 root = center + out * rng.range(0.05f, 0.3f) / float(tufts);
                float height = tuft_height * rng.range(0.6f, 1.f);
                float reach = params.lean * rng.range(0.2f, 1.f) * height;
                float half_width = 0.5f * params.width * rng.range(0.7f, 1.3f);

                // A blade bends along its face, more or less away from the tuft
                float facing = azimuth + rng.range(-0.6f, 0.6f);
                float id = rng.next();
                glm::vec2 face(std::cos(facing), std::sin(facing));
                glm::vec2 side(-face.y, face.x);

                auto spine = [&](float s) {
                    glm::vec2 p = root + out * reach * (0.25f * s + 0.75f * s * s);
                    return glm::vec3(p.x, height * s, p.y);
                };

                // Left, middle (the fold of the V) and right vertices of each level, then the tip
                uint32_t base = static_cast<uint32_t>(model.vertices.size());
                for(int level = 0; level < levels; level++) {
                    float s = float(level) / levels;
                    float w = half_width * std::pow(1.f - s, 0.6f);
                    glm::vec3 c = spine(s);
                    glm::vec3 side3(side.x, 0.f, side.y), face3(face.x, 0.f, face.y);
                    model.vertices.push_back(c - side3 * w);
                    model.vertices.push_back(c - face3 * (params.fold * w));
                    model.vertices.push_back(c + side3 * w);
                }
                uint32_t tip = static_cast<uint32_t>(model.vertices.size());
                model.vertices.push_back(spine(1.f));
                model.ids.resize(model.vertices.size(), id);

                for(int level = 0; level + 1 < levels; level++) {
                    uint32_t l0 = base + level * 3, m0 = l0 + 1, r0 = l0 + 2;
                    uint32_t l1 = l0 + 3, m1 = l1 + 1, r1 = l1 + 2;
                    for(uint32_t index : {l0, m0, l1, m0, m1, l1, m0, r0, m1, r0, r1, m1}) {
                        model.indices.push_back(index);
                    }
                }
                uint32_t l = base + (levels - 1) * 3;
                for(uint32_t index : {l, l + 1, tip, l + 1, l + 2, tip}) {
                    model.indices.push_back(index);
                }

                model.blades++;

            }
        }
    }

    model.parts.assign(model.vertices.size(), static_cast<uint8_t>(GrassPart::Blade));

}

}

const GrassKindInfo& grassKindInfo(GrassKind kind) {
    return kinds[std::min<uint32_t>(static_cast<uint32_t>(kind), grass_kinds - 1)];
}

bool grassKindFromName(const std::string& name, GrassKind& kind) {
    for(int i = 0; i < grass_kinds; i++) {
        if(name == kinds[i].name) {
            kind = static_cast<GrassKind>(i);
            return true;
        }
    }
    return false;
}

GrassModel generateGrassTile(const GrassModelParams& params) {

    GrassModel model;
    Rng rng(params.seed);

    // The other kinds are modelled in metres on a tile of their own size
    const float tile = grassKindInfo(params.kind).tile_size;
    Builder builder { model };
    switch(params.kind) {
        case GrassKind::Fescue: fescue(builder, rng, tile); break;
        case GrassKind::Cocksfoot: cocksfoot(builder, rng, tile); break;
        case GrassKind::Ryegrass: ryegrass(builder, rng, tile); break;
        case GrassKind::Timothy: timothy(builder, rng, tile); break;
        case GrassKind::Oat: oat(builder, rng, tile); break;
        case GrassKind::Clover: cloverLeaves(builder, rng, tile); break;
        case GrassKind::CloverFlower: cloverLeaves(builder, rng, tile); cloverHeads(builder, rng, tile); break;
        case GrassKind::Plantain: plantain(builder, rng, tile); break;
        case GrassKind::Dandelion: dandelion(builder, rng, tile); break;
        default: meadow(model, rng, params); break;
    }

    // The tallest tip at max_height
    float top = 0.f;
    for(auto& v : model.vertices) top = std::max(top, v.y);
    if(top > 0.f) {
        for(auto& v : model.vertices) v.y *= params.max_height / top;
    }
    model.max_height = params.max_height;
    if(params.kind != GrassKind::Meadow) {
        for(auto& v : model.vertices) {
            v.x /= tile;
            v.z /= tile;
        }
        model.tile_size = tile;
        model.height = top;
    }

    // Smooth normals, weighted by the triangle areas (blades share no vertex with each other)
    model.normals.assign(model.vertices.size(), glm::vec3(0.f));
    for(size_t i = 0; i + 2 < model.indices.size(); i += 3) {
        uint32_t a = model.indices[i], b = model.indices[i + 1], c = model.indices[i + 2];
        glm::vec3 n = glm::cross(model.vertices[b] - model.vertices[a], model.vertices[c] - model.vertices[a]);
        model.normals[a] += n;
        model.normals[b] += n;
        model.normals[c] += n;
    }
    for(auto& n : model.normals) {
        float length = glm::length(n);
        n = length > 0.f ? n / length : glm::vec3(0.f, 1.f, 0.f);
    }

    // Green by the height for what is green, the colours of the kind for the rest
    const GrassKindInfo& info = grassKindInfo(params.kind);
    model.colours.resize(model.vertices.size());
    for(size_t i = 0; i < model.vertices.size(); i++) {
        float y = model.vertices[i].y / model.max_height;
        glm::vec3 green = glm::mix(grass_root_colour, grass_tip_colour, y);
        switch(static_cast<GrassPart>(model.parts[i])) {
            case GrassPart::Blade: model.colours[i] = green; break;
            case GrassPart::Stem: model.colours[i] = glm::mix(green, glm::vec3(0.4f, 0.48f, 0.22f), 0.45f); break;
            case GrassPart::Leaf: model.colours[i] = glm::mix(grass_root_colour, grass_tip_colour, 0.3f + 0.6f * y) * glm::vec3(0.8f, 1.f, 0.95f); break;
            case GrassPart::Flower: model.colours[i] = info.flower; break;
            case GrassPart::Seed: model.colours[i] = info.seed; break;
        }
    }

    return model;

}

bool writeGrassObj(const GrassModel& model, const std::string& path, bool metres) {

    std::ofstream os(path);
    if(!os.good()) return false;

    // Normals go by the inverse of the scale
    glm::vec3 scale = metres ? glm::vec3(model.tile_size, model.height / model.max_height, model.tile_size) : glm::vec3(1.f);

    os << "# Dynamical grass tile : " << model.blades << " blades, " << scale.x << " x " << scale.z << " on the ground, y up\n";
    for(auto& v : model.vertices) os << "v " << v.x * scale.x << " " << v.y * scale.y << " " << v.z * scale.z << "\n";
    for(auto& normal : model.normals) {
        glm::vec3 n = glm::normalize(normal / scale);
        os << "vn " << n.x << " " << n.y << " " << n.z << "\n";
    }
    const char* names[] = {"blade", "stem", "leaf", "flower", "seed"};
    for(uint8_t part = 0; part < 5; part++) {
        bool named = false;
        for(size_t i = 0; i + 2 < model.indices.size(); i += 3) {
            if(model.parts[model.indices[i]] != part) continue;
            if(!named) os << "g " << names[part] << "\n";
            named = true;
            os << "f";
            for(int k = 0; k < 3; k++) {
                uint32_t index = model.indices[i + k] + 1;
                os << " " << index << "//" << index;
            }
            os << "\n";
        }
    }

    return os.good();

}

}
