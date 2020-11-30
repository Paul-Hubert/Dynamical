#ifndef OBJECT_RENDER_H
#define OBJECT_RENDER_H

#include "renderer/vk.h"

#include "renderer/vmapp.h"

#include "glm/glm.hpp"

#include "entt/entt.hpp"

class Context;
class Renderpass;
class ViewUBO;

class ObjectRender {
public:
    ObjectRender(entt::registry& reg, Context& ctx, Renderpass& renderpass, std::vector<vk::DescriptorSetLayout> layouts);
    void render(vk::CommandBuffer command, uint32_t index);
    ~ObjectRender();

    void createPipeline(std::vector<vk::DescriptorSetLayout> layouts);

    vk::DescriptorPool pool;
    vk::DescriptorSetLayout set_layout;
    struct per_frame {
        vk::DescriptorSet set;
        VmaBuffer buffer;
    };
    std::vector<per_frame> per_frame;

    vk::PipelineLayout layout;
    vk::Pipeline pipeline;

private:
    entt::registry& reg;
    Context& ctx;
    Renderpass& renderpass;

};

#endif
