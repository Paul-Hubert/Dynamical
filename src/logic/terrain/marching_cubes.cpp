#include "marching_cubes.h"

#include "util/util.h"

#include <cstring>
#include <stdexcept>

using namespace dy;

namespace {

// Corner i of a cube, bit i of the configuration index (matches the compute shader)
const glm::ivec3 corners[8] = {
    {0,0,0}, {1,0,0}, {1,0,1}, {0,0,1},
    {0,1,0}, {1,1,0}, {1,1,1}, {0,1,1},
};

// Corner pairs of the 12 cube edges
const int edges[12][2] = {
    {0,1}, {1,2}, {2,3}, {3,0},
    {4,5}, {5,6}, {6,7}, {7,4},
    {0,4}, {1,5}, {2,6}, {3,7},
};

}

MarchingCubes::MarchingCubes(const std::string& table_path) {
    auto data = dy::readFile(table_path.c_str());
    if(data.size() != table.size()) {
        throw std::runtime_error(table_path + " is missing or corrupted");
    }
    std::memcpy(table.data(), data.data(), table.size());
}

void MarchingCubes::mesh(const float* values, glm::ivec3 samples, float cube_size, IndexedMesh& out) const {

    out.vertices.clear();
    out.indices.clear();

    auto value = [&](glm::ivec3 p) {
        return values[((size_t) p.x * samples.z + p.z) * samples.y + p.y];
    };

    // One vertex per crossed grid edge : index by the lower corner and the edge axis
    const size_t num_samples = (size_t) samples.x * samples.y * samples.z;
    std::vector<int32_t> edge_vertex(3 * num_samples, -1);

    auto vertexOn = [&](glm::ivec3 a, glm::ivec3 b) -> uint32_t {
        glm::ivec3 low = glm::min(a, b);
        int axis = a.x != b.x ? 0 : (a.y != b.y ? 1 : 2);
        size_t key = axis * num_samples + ((size_t) low.x * samples.z + low.z) * samples.y + low.y;
        if(edge_vertex[key] < 0) {
            float da = value(a), db = value(b);
            float t = da / (da - db);
            edge_vertex[key] = (int32_t) out.vertices.size();
            out.vertices.push_back(glm::mix(glm::vec3(a), glm::vec3(b), t) * cube_size);
        }
        return (uint32_t) edge_vertex[key];
    };

    // The table winds every triangle the same way : check it against the density gradient once per mesh
    double orientation = 0.;

    for(int x = 0; x + 1 < samples.x; x++) {
        for(int z = 0; z + 1 < samples.z; z++) {
            for(int y = 0; y + 1 < samples.y; y++) {

                glm::ivec3 cube(x, y, z);
                float d[8];
                uint32_t config = 0;
                for(int i = 0; i < 8; i++) {
                    d[i] = value(cube + corners[i]);
                    if(d[i] > 0.f) config |= 1u << i;
                }
                if(config == 0 || config == 255) continue;

                // Density grows toward the inside of the solid
                glm::vec3 gradient(
                    d[1] + d[2] + d[5] + d[6] - d[0] - d[3] - d[4] - d[7],
                    d[4] + d[5] + d[6] + d[7] - d[0] - d[1] - d[2] - d[3],
                    d[2] + d[3] + d[6] + d[7] - d[0] - d[1] - d[4] - d[5]
                );

                const int8_t* entry = &table[config * 16];
                for(int i = 0; i < 15 && entry[i] >= 0; i += 3) {
                    uint32_t tri[3];
                    for(int k = 0; k < 3; k++) {
                        int edge = entry[i + k];
                        tri[k] = vertexOn(cube + corners[edges[edge][0]], cube + corners[edges[edge][1]]);
                    }
                    if(tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2]) continue;

                    glm::vec3 a = out.vertices[tri[0]], b = out.vertices[tri[1]], c = out.vertices[tri[2]];
                    orientation -= glm::dot(glm::cross(b - a, c - a), gradient);

                    out.indices.insert(out.indices.end(), {tri[0], tri[1], tri[2]});
                }

            }
        }
    }

    if(orientation < 0.) {
        for(size_t i = 0; i < out.indices.size(); i += 3) {
            std::swap(out.indices[i + 1], out.indices[i + 2]);
        }
    }

}

glm::vec3 MarchingCubes::vertexNormal(const std::function<float(glm::ivec3)>& density, glm::ivec3 low, int axis) const {

    glm::ivec3 along(0), u(0), v(0);
    along[axis] = 1;
    u[(axis + 1) % 3] = 1;
    v[(axis + 2) % 3] = 1;

    glm::vec3 sum(0.f);

    // The same order whichever cube asks : every copy of the vertex gets the same normal
    for(int k = 0; k < 4; k++) {

        glm::ivec3 cube = low - u * (k & 1) - v * (k >> 1);

        float d[8];
        uint32_t config = 0;
        for(int i = 0; i < 8; i++) {
            d[i] = density(cube + corners[i]);
            if(d[i] > 0.f) config |= 1u << i;
        }

        // The edge, among the twelve of this cube
        int shared = -1;
        for(int e = 0; e < 12; e++) {
            glm::ivec3 a = corners[edges[e][0]], b = corners[edges[e][1]];
            if(glm::min(a, b) == low - cube && glm::abs(b - a) == along) shared = e;
        }

        // Where the ground crosses an edge of the cube, in cubes from low
        auto vertexOn = [&](int e) {
            int a = edges[e][0], b = edges[e][1];
            float t = -d[a] / (d[b] - d[a]);
            return glm::mix(glm::vec3(cube + corners[a] - low), glm::vec3(cube + corners[b] - low), t);
        };

        const int8_t* entry = &table[config * 16];
        for(int i = 0; i < 15 && entry[i] >= 0; i += 3) {
            if(entry[i] != shared && entry[i + 1] != shared && entry[i + 2] != shared) continue;
            glm::vec3 a = vertexOn(entry[i]), b = vertexOn(entry[i + 1]), c = vertexOn(entry[i + 2]);
            sum += glm::cross(b - a, c - a);
        }

    }

    // The triangles of the table are counter clockwise seen from outside the solid (TERRAIN.1, TERRAIN.2)
    return sum;

}
