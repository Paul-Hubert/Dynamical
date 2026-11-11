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
        Volume = 3,     // a box : the baked volume of a species
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

// A plant far from the viewers is a box in the rays, which read the baked volume of its species in it
// (PlantVolumes) : the unit box of RayScene placed over the bounds of the species
struct RayBox {
    int32_t volume = -1;                // index of the textures of the volume (bindings 10 to 12 of the view set)
    glm::vec3 min = glm::vec3(0.f);     // bounds of the species, in its own space
    glm::vec3 size = glm::vec3(1.f);
    float voxel = 1.f;                  // metres a side of a texel of the volume
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

    uint32_t add(RayBox box) {
        boxes.push_back(box);
        return (uint32_t) boxes.size() - 1;
    }

    std::vector<RayMesh> meshes;
    std::vector<RayBox> boxes;
    std::vector<vk::DescriptorImageInfo> textures;
};

// Context variable : a mesh to put in the scene of this frame on its own, where only the rays of a bake see it
// (PlantVolumes asks, RayScene answers)
struct RayBake {
    int32_t mesh = -1;                  // of RayMeshes, -1 : none
    glm::vec3 origin = glm::vec3(0.f);  // where its instance goes
    bool placed = false;                // it is in the top level structure of this frame
};

// Context variable written by VegetationPass : the mesh of each species of the library, -1 while it is not loaded
struct RayPlantMeshes {
    std::vector<int32_t> mesh;
    std::vector<int32_t> box;       // of RayMeshes::boxes, -1 while the volume of the species is not baked
    std::vector<glm::vec3> min, max;    // bounds of the most detailed level of each species
};

// Context variable written by PlantVolumes : the baked volume of each species of the library, for the pass
// that draws the plants far away from it
struct PlantVolumeSet {
    struct Volume {
        int32_t index = -1;                 // of its textures in bindings 10 to 12 of the view set, -1 : not baked
        glm::vec3 min = glm::vec3(0.f);     // bounds, in the space of the plant
        glm::vec3 size = glm::vec3(1.f);
        float voxel = 1.f;                  // metres a texel
    };
    std::vector<Volume> species;
};

// A plant that is its leaves in the rays (within the plant geometry range of the tiers, with a margin to leave)
struct PlantInRays {};

// Where the instances of a plant go in the top level structure, worked out once (plants do not move) : its
// mesh, and the box of its species over its bounds
struct PlantRayTransforms {
    float mesh[12];     // three rows of four
    float box[12];
    int32_t boxed = -1; // the RayBox `box` was made for
};

}

#endif
