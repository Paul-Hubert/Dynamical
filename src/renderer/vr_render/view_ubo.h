#ifndef UBO_DESCRIPTOR_H
#define UBO_DESCRIPTOR_H

#include "renderer/vk.h"

#include "renderer/vmapp.h"

#include "renderer/num_frames.h"

#include "glm/glm.hpp"

struct UBO {

    glm::mat4 view_projection;
    glm::vec4 position;
    
};

class Context;

class ViewUBO {
public:
    ViewUBO(Context& ctx);
    ~ViewUBO();
    
    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;

    struct View {
        std::vector<vk::DescriptorSet> descSets;

        std::array<VmaBuffer, NUM_FRAMES> ubos;
        std::array<UBO*, NUM_FRAMES> pointers;
    };
    std::vector<View> views;

private:
    Context& ctx;
    
};

#endif
