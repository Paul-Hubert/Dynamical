#ifndef DY_MARCHING_CUBES_CPU_H
#define DY_MARCHING_CUBES_CPU_H

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
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

private:
    std::array<int8_t, 256 * 16> table;
};

}

#endif
