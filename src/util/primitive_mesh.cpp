#include "primitive_mesh.h"

#include <cmath>

using namespace dy;

namespace {

constexpr float pi = 3.14159265358979f;

// Rings of vertices from the top pole to the bottom pole; each ring is (segments + 1) vertices (seam duplicated)
void addRing(MeshData& m, float y, float ring_radius, glm::vec3 center, float radius, int segments, float v) {
    for(int s = 0; s <= segments; s++) {
        float a = 2.f * pi * s / segments;
        glm::vec3 p(ring_radius * std::cos(a), y, -ring_radius * std::sin(a));
        m.positions.push_back(p);
        glm::vec3 n = p - center;
        float len = glm::length(n);
        m.normals.push_back(len > 0.f ? n / len : glm::vec3(0.f, center.y >= 0.f ? 1.f : -1.f, 0.f));
        m.uvs.push_back(glm::vec2((float) s / segments, v));
    }
}

void stitch(MeshData& m, int rings, int segments) {
    const int stride = segments + 1;
    for(int r = 0; r + 1 < rings; r++) {
        for(int s = 0; s < segments; s++) {
            uint32_t a = r * stride + s, b = a + 1, c = a + stride, d = c + 1;
            m.indices.insert(m.indices.end(), { a, c, b, b, c, d });
        }
    }
}

}

MeshData dy::capsuleMesh(float radius, float half_height, int segments, int cap_rings) {

    MeshData m;
    int rings = 0;
    const int total = 2 * (cap_rings + 1);

    // Top cap: from the pole down to the equator, around (0, half_height, 0)
    for(int r = 0; r <= cap_rings; r++, rings++) {
        float t = (pi / 2.f) * r / cap_rings;
        addRing(m, half_height + radius * std::cos(t), radius * std::sin(t), glm::vec3(0.f, half_height, 0.f), radius, segments, (float) rings / (total - 1));
    }
    // Bottom cap: from the equator down to the pole, around (0, -half_height, 0)
    for(int r = 0; r <= cap_rings; r++, rings++) {
        float t = (pi / 2.f) + (pi / 2.f) * r / cap_rings;
        addRing(m, -half_height + radius * std::cos(t), radius * std::sin(t), glm::vec3(0.f, -half_height, 0.f), radius, segments, (float) rings / (total - 1));
    }

    stitch(m, rings, segments);
    return m;

}

MeshData dy::sphereMesh(float radius, int segments, int rings) {
    MeshData m;
    for(int r = 0; r <= rings; r++) {
        float t = pi * r / rings;
        addRing(m, radius * std::cos(t), radius * std::sin(t), glm::vec3(0.f), radius, segments, (float) r / rings);
    }
    stitch(m, rings + 1, segments);
    return m;
}
