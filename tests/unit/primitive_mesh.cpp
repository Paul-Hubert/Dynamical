#include <doctest/doctest.h>

#include <algorithm>
#include <glm/glm.hpp>

#include "util/primitive_mesh.h"

using namespace dy;

namespace {

float segmentDistance(glm::vec3 p, float half_height) {
    glm::vec3 q(0.f, std::clamp(p.y, -half_height, half_height), 0.f);
    return glm::length(p - q);
}

}

TEST_SUITE("unit") {

TEST_CASE("T2.5 generated capsule vertices lie on the capsule surface") {

    const float cases[][2] = { { 0.05f, 0.2f }, { 0.12f, 0.35f }, { 0.5f, 0.f }, { 0.3f, 1.5f } };
    for(auto& c : cases) {
        float radius = c[0], half_height = c[1];
        MeshData m = capsuleMesh(radius, half_height);
        REQUIRE(!m.positions.empty());
        float worst = 0.f;
        float top = -1e9f, bottom = 1e9f;
        for(auto& p : m.positions) {
            worst = std::max(worst, std::abs(segmentDistance(p, half_height) - radius));
            top = std::max(top, p.y);
            bottom = std::min(bottom, p.y);
        }
        CAPTURE(radius);
        CAPTURE(half_height);
        CHECK(worst <= 1e-4f);
        // The long axis is local Y, as Jolt's CapsuleShape
        CHECK(top == doctest::Approx(half_height + radius).epsilon(1e-5));
        CHECK(bottom == doctest::Approx(-half_height - radius).epsilon(1e-5));
        CHECK(m.indices.size() % 3 == 0);
        for(auto i : m.indices) CHECK(i < m.positions.size());
    }

}

}
