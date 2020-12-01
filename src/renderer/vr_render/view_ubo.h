#ifndef UBO_DESCRIPTOR_H
#define UBO_DESCRIPTOR_H

#include "renderer/util/vk.h"

#include "renderer/util/vmapp.h"

#include "renderer/context/num_frames.h"

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
        struct PerFrame {
            vk::DescriptorSet set;
            VmaBuffer ubo;
            UBO* pointer;
        };
        std::vector<PerFrame> per_frame;
    };
    std::vector<View> views;

private:
    Context& ctx;
    
};

#endif
