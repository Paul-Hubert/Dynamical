#ifndef DY_MODEL_H
#define DY_MODEL_H

#include <glm/glm.hpp>

#include <memory>
#include <vector>

#include "renderer/util/vmapp.h"

namespace dy {

struct ModelVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// GPU mesh of a loaded glTF file, node transforms baked in.
// Lives on its own entity, instances point to it with ModelRenderable.
class ModelC {
public:

    struct Material {
        glm::vec4 base_color = glm::vec4(1.f);
        std::shared_ptr<VmaImage> image;       // null : untextured, uses the default white texture
        vk::DescriptorSet set;                 // set 1 of the model pipeline, filled in by ModelPass
    };

    struct Part {
        uint32_t first_index;
        uint32_t index_count;
        uint32_t material;
    };

    std::shared_ptr<VmaBuffer> vertices;
    std::shared_ptr<VmaBuffer> indices;
    std::vector<Part> parts;
    std::vector<Material> materials;

    glm::vec3 min = glm::vec3(0.f);
    glm::vec3 max = glm::vec3(0.f);

    int32_t ray_mesh = -1;      // in RayMeshes, with ray tracing : given by ModelPass

};

}

#endif
