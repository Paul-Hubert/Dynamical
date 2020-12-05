#ifndef OBJECT_RENDER_H
#define OBJECT_RENDER_H

#include "renderer/util/vk.h"

#include "renderer/util/vmapp.h"

#include "glm/glm.hpp"

#include "entt/entt.hpp"

class Context;

class ObjectRender {
public:
    ObjectRender(entt::registry& reg, Context& ctx, vk::RenderPass renderpass, std::vector<vk::DescriptorSetLayout> layouts);
    void render(vk::CommandBuffer command, uint32_t index);
    ~ObjectRender();

    void createPipeline(vk::RenderPass renderpass, std::vector<vk::DescriptorSetLayout> layouts);

    void updateBuffer(int i);

    struct Transform {
        glm::mat4 mat;
        glm::vec4 pos;
    };

    vk::DescriptorPool pool;
    vk::DescriptorSetLayout set_layout;
    struct per_frame {
        vk::DescriptorSet set;
        int capacity = 100;
        VmaBuffer buffer;
        Transform* pointer;
    };
    std::vector<per_frame> per_frame;

    vk::PipelineLayout layout;
    vk::Pipeline pipeline;

private:
    entt::registry& reg;
    Context& ctx;

};

#endif
