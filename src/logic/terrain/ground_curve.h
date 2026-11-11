#ifndef DY_GROUND_CURVE_H
#define DY_GROUND_CURVE_H

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace dy {

// Past this the ground is no ground to stand on (and the grid of the densities cannot tell more)
constexpr float ground_curve_max = 1.f;

// How the ground bends at a sample of the terrain densities : second derivatives of its height y = h(x, z),
// (xx, xz, zz), per metre. Negative along a direction where the ground falls away on both sides (a crest),
// positive in a hollow. From the first and second differences of the density, which is 0 on the ground :
// h is the implicit function of density(x, h, z) = 0. Zero where the ground is too steep to be a height.
// density(p) : value at the sample p (it needs the 18 samples around it too), spacing : metres between samples.
// Mirrored by marchingcubes.comp.glsl, which gives it to every vertex of the terrain
template<typename Density>
glm::vec3 groundCurve(Density&& density, glm::ivec3 p, float spacing) {

    auto at = [&](int x, int y, int z) {
        return density(p + glm::ivec3(x, y, z));
    };

    float center = at(0, 0, 0);
    float h = 1.f / spacing, hh = h * h;
    glm::vec3 first = glm::vec3(at(1, 0, 0) - at(-1, 0, 0), at(0, 1, 0) - at(0, -1, 0), at(0, 0, 1) - at(0, 0, -1)) * (0.5f * h);
    if(first.y * first.y < 0.09f * glm::dot(first, first) || first.y == 0.f) {
        return glm::vec3(0.f);
    }

    float xx = (at(1, 0, 0) - 2.f * center + at(-1, 0, 0)) * hh;
    float yy = (at(0, 1, 0) - 2.f * center + at(0, -1, 0)) * hh;
    float zz = (at(0, 0, 1) - 2.f * center + at(0, 0, -1)) * hh;
    float xy = (at(1, 1, 0) - at(1, -1, 0) - at(-1, 1, 0) + at(-1, -1, 0)) * (0.25f * hh);
    float yz = (at(0, 1, 1) - at(0, 1, -1) - at(0, -1, 1) + at(0, -1, -1)) * (0.25f * hh);
    float xz = (at(1, 0, 1) - at(1, 0, -1) - at(-1, 0, 1) + at(-1, 0, -1)) * (0.25f * hh);

    // Slopes of the ground, then how they change
    float sx = -first.x / first.y, sz = -first.z / first.y;
    glm::vec3 curve(
        -(xx + 2.f * xy * sx + yy * sx * sx) / first.y,
        -(xz + xy * sz + yz * sx + yy * sx * sz) / first.y,
        -(zz + 2.f * yz * sz + yy * sz * sz) / first.y
    );
    return glm::clamp(curve, glm::vec3(-ground_curve_max), glm::vec3(ground_curve_max));

}

}

#endif
