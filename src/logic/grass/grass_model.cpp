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

const float two_pi = 6.28318530718f;

}

GrassModel generateGrassTile(const GrassModelParams& params) {

    GrassModel model;
    Rng rng(params.seed);

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

    // The tallest tip at max_height
    float top = 0.f;
    for(auto& v : model.vertices) top = std::max(top, v.y);
    if(top > 0.f) {
        for(auto& v : model.vertices) v.y *= params.max_height / top;
    }
    model.max_height = params.max_height;

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

    return model;

}

bool writeGrassObj(const GrassModel& model, const std::string& path) {

    std::ofstream os(path);
    if(!os.good()) return false;

    os << "# Dynamical grass tile : " << model.blades << " blades, 1 x 1 on the ground, y up\n";
    for(auto& v : model.vertices) os << "v " << v.x << " " << v.y << " " << v.z << "\n";
    for(auto& n : model.normals) os << "vn " << n.x << " " << n.y << " " << n.z << "\n";
    for(size_t i = 0; i + 2 < model.indices.size(); i += 3) {
        os << "f";
        for(int k = 0; k < 3; k++) {
            uint32_t index = model.indices[i + k] + 1;
            os << " " << index << "//" << index;
        }
        os << "\n";
    }

    return os.good();

}

}
