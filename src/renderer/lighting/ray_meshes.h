#ifndef DY_RAY_MESHES_H
#define DY_RAY_MESHES_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace dy {

// A mesh a pass wants in the rays of the lighting : RayScene builds an acceleration structure from it, once,
// and instances it where the entities that show it are. Its buffers were created with
// Context::rayInputUsage(). A vertex starts with its position (three floats), indices are 32 bits and
// relative to first_vertex
struct RayMesh {
    // How the shaders find the texture coordinates of a hit, for the alpha test of the leaves
    enum class Kind : uint32_t {
        Terrain = 0,    // the chunks : no table
        Solid = 1,      // nothing to read in its vertices
        Plant = 2,      // VegetationVertex
    };

    struct Part {
        uint32_t first_index = 0, index_count = 0;
        bool opaque = true;                             // false : only where the alpha of `texture` is over alpha_cutoff
        int32_t texture = -1;                           // RayMeshes::texture()
        float alpha_cutoff = 0.f;
        glm::vec3 color = glm::vec3(0.5f);              // of the light it sends back
        glm::vec3 transmission = glm::vec3(0.f);        // of the light that goes through a part that is not opaque
    };

    Kind kind = Kind::Solid;
    std::shared_ptr<VmaBuffer> vertices, indices;
    uint32_t vertex_stride = 0;
    uint32_t first_vertex = 0, vertex_count = 0;
    std::vector<Part> parts;
};

// Context variable, only with ray tracing : the meshes and the textures the passes gave to the rays.
// Main thread
class RayMeshes {
public:
    // Stable identifiers : nothing is ever taken back (species and models stay loaded)
    uint32_t add(RayMesh mesh) {
        meshes.push_back(std::move(mesh));
        return (uint32_t) meshes.size() - 1;
    }

    // A texture the rays sample (binding 6 of the view set), its index for RayMesh::Part
    int32_t texture(vk::ImageView view, vk::Sampler sampler) {
        for(size_t i = 0; i < textures.size(); i++) {
            if(textures[i].imageView == view) return (int32_t) i;
        }
        if(textures.size() >= max_textures) return -1;
        textures.push_back(vk::DescriptorImageInfo(sampler, view, vk::ImageLayout::eShaderReadOnlyOptimal));
        return (int32_t) textures.size() - 1;
    }

    static constexpr uint32_t max_textures = 512;

    std::vector<RayMesh> meshes;
    std::vector<vk::DescriptorImageInfo> textures;
};

// Context variable written by VegetationPass : the mesh of each species of the library, -1 while it is not loaded
struct RayPlantMeshes {
    std::vector<int32_t> mesh;
};

// A plant that is its leaves in the rays (within the plant geometry range of the tiers, with a margin to leave)
struct PlantInRays {};

}

#endif
