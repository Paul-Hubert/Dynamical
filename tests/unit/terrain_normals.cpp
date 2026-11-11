#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "logic/terrain/marching_cubes.h"

using namespace dy;

namespace {

// Densities of a block of samples laid out [x][z][y], as MarchingCubes::mesh takes them
struct Block {
    glm::ivec3 samples;
    std::vector<float> values;

    template<typename Density>
    Block(glm::ivec3 samples, Density&& density) : samples(samples), values((size_t) samples.x * samples.y * samples.z) {
        for(int x = 0; x < samples.x; x++) {
            for(int z = 0; z < samples.z; z++) {
                for(int y = 0; y < samples.y; y++) {
                    values[((size_t) x * samples.z + z) * samples.y + y] = density(glm::vec3(x, y, z));
                }
            }
        }
    }

    float at(glm::ivec3 p) const {
        return values[((size_t) p.x * samples.z + p.z) * samples.y + p.y];
    }
};

// A vertex of the mesh and the grid edge it is on
struct EdgeVertex {
    uint32_t index;
    glm::ivec3 low;
    int axis;
};

// Vertices far enough from the sides of the block to have their four cubes in it, and not on a sample
// (where the edge cannot be told from the position)
std::vector<EdgeVertex> innerVertices(const Block& block, const IndexedMesh& mesh, float cube_size) {
    std::vector<EdgeVertex> out;
    for(uint32_t i = 0; i < mesh.vertices.size(); i++) {
        glm::vec3 g = mesh.vertices[i] / cube_size;
        glm::vec3 off = glm::abs(g - glm::round(g));
        int axis = off.x > off.y ? (off.x > off.z ? 0 : 2) : (off.y > off.z ? 1 : 2);
        if(off[axis] < 1e-3f) continue;
        glm::ivec3 low = glm::ivec3(glm::round(g));
        low[axis] = (int) std::floor(g[axis]);
        if(glm::any(glm::lessThan(low, glm::ivec3(1))) || glm::any(glm::greaterThan(low + 2, block.samples - 1))) continue;
        out.push_back({i, low, axis});
    }
    return out;
}

// Sum of the normals of the triangles of the mesh around each vertex, twice their area in cubes² long
std::vector<glm::vec3> triangleSums(const IndexedMesh& mesh, float cube_size) {
    std::vector<glm::vec3> sums(mesh.vertices.size(), glm::vec3(0.f));
    for(size_t i = 0; i < mesh.indices.size(); i += 3) {
        glm::vec3 a = mesh.vertices[mesh.indices[i]], b = mesh.vertices[mesh.indices[i + 1]], c = mesh.vertices[mesh.indices[i + 2]];
        glm::vec3 normal = glm::cross(b - a, c - a) / (cube_size * cube_size);
        for(int k = 0; k < 3; k++) {
            sums[mesh.indices[i + k]] += normal;
        }
    }
    return sums;
}

}

TEST_CASE("TERRAIN.1 the normal of a terrain vertex is the sum of the triangles around it") {

    MarchingCubes cubes;
    const float cube_size = 4.f;
    const glm::ivec3 samples(18, 17, 19);

    // Hills with overhangs : many configurations of the cubes
    Block block(samples, [](glm::vec3 p) {
        return 7.3f - p.y + 2.1f * std::sin(p.x * 0.9f) * std::cos(p.z * 0.7f) + 1.7f * std::sin(p.y * 1.3f + p.x * 0.5f) * std::sin(p.z * 1.1f);
    });
    auto density = [&](glm::ivec3 p) { return block.at(p); };

    IndexedMesh mesh;
    cubes.mesh(block.values.data(), samples, cube_size, mesh);
    REQUIRE(mesh.indices.size() > 3000);

    std::vector<glm::vec3> sums = triangleSums(mesh, cube_size);
    std::vector<EdgeVertex> vertices = innerVertices(block, mesh, cube_size);
    REQUIRE(vertices.size() > 500);

    // The mesh is wound counter clockwise seen from outside : the same vector, out of the ground
    float worst = 0.f;
    for(auto& vertex : vertices) {
        glm::vec3 normal = cubes.vertexNormal(density, vertex.low, vertex.axis);
        worst = std::max(worst, glm::length(normal - sums[vertex.index]));
    }
    CHECK(worst < 1e-3f);

    // No triangle on an edge the ground does not cross
    Block air(glm::ivec3(4), [](glm::vec3 p) { return -1.f - p.y; });
    CHECK(cubes.vertexNormal([&](glm::ivec3 p) { return air.at(p); }, glm::ivec3(1), 1) == glm::vec3(0.f));

}

TEST_CASE("TERRAIN.2 vertex normals of a sphere point away from its centre, up on flat ground") {

    MarchingCubes cubes;
    const float cube_size = 4.f;
    const glm::ivec3 samples(20);
    const glm::vec3 centre(9.6f, 9.3f, 10.1f);
    const float radius = 6.4f;

    Block block(samples, [&](glm::vec3 p) { return radius - glm::length(p - centre); });
    auto density = [&](glm::ivec3 p) { return block.at(p); };

    IndexedMesh mesh;
    cubes.mesh(block.values.data(), samples, cube_size, mesh);
    std::vector<EdgeVertex> vertices = innerVertices(block, mesh, cube_size);
    REQUIRE(vertices.size() > 300);

    float worst = 1.f;
    for(auto& vertex : vertices) {
        glm::vec3 normal = glm::normalize(cubes.vertexNormal(density, vertex.low, vertex.axis));
        glm::vec3 radial = glm::normalize(mesh.vertices[vertex.index] / cube_size - centre);
        worst = std::min(worst, glm::dot(normal, radial));
    }
    CHECK(worst > std::cos(glm::radians(8.f)));

    Block flat(glm::ivec3(6), [](glm::vec3 p) { return 2.4f - p.y; });
    glm::vec3 up = cubes.vertexNormal([&](glm::ivec3 p) { return flat.at(p); }, glm::ivec3(2, 2, 3), 1);
    CHECK(up.x == doctest::Approx(0.f));
    CHECK(up.z == doctest::Approx(0.f));
    CHECK(up.y > 1.f);    // six triangles or so, half a cube² each

}
