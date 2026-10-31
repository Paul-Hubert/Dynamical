#ifndef DY_PRIMITIVE_MESH_H
#define DY_PRIMITIVE_MESH_H

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace dy {

struct MeshData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<uint32_t> indices;      // counter-clockwise seen from outside
};

// Capsule along local Y, as Jolt's CapsuleShape: a cylinder of half-height half_height capped by two hemispheres
// of radius radius. Generated per (radius, half-height): scaling a unit capsule would distort the caps
MeshData capsuleMesh(float radius, float half_height, int segments = 24, int cap_rings = 8);

// Sphere of the given radius centered on the origin
MeshData sphereMesh(float radius, int segments = 24, int rings = 16);

}

#endif
