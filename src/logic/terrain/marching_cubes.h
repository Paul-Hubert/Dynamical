#ifndef DY_MARCHING_CUBES_CPU_H
#define DY_MARCHING_CUBES_CPU_H

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dy {

struct IndexedMesh {
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices; // 3 per triangle, counter clockwise seen from outside the solid
};

// CPU marching cubes, same edge table (resources/mclookup.bin) and corner order as marchingcubes.comp.glsl.
// Unlike the GPU version, vertices are shared between cubes (one per crossed edge), which physics engines
// need to find internal edges. Thread safe once loaded.
class MarchingCubes {
public:
    // Throws if the table is missing
    explicit MarchingCubes(const std::string& table_path = "./resources/mclookup.bin");

    // values : samples.x * samples.y * samples.z densities laid out [x][z][y], > 0 is solid.
    // Vertices are relative to sample (0, 0, 0), in world units.
    void mesh(const float* values, glm::ivec3 samples, float cube_size, IndexedMesh& out) const;

    // Normal of the vertex on a grid edge (low : its sample of lower coordinates, axis : 0 x, 1 y, 2 z) :
    // the sum of the normals of the triangles that meet there, each as long as twice its area in cubes²,
    // pointing out of the solid. Not normalised; zero when the edge is not crossed. Those triangles are in the
    // four cubes around the edge : density(p) is asked for the samples from low - 1 to low + 2.
    // What marchingcubes.comp.glsl gives to every vertex of the terrain (vertexNormal there)
    glm::vec3 vertexNormal(const std::function<float(glm::ivec3)>& density, glm::ivec3 low, int axis) const;

private:
    std::array<int8_t, 256 * 16> table;
};

}

#endif
