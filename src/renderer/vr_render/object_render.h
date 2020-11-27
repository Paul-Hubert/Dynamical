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
    void render(vk::CommandBuffer command);
    ~ObjectRender();

    void createPipeline(std::vector<vk::DescriptorSetLayout> layouts);

    operator vk::Pipeline() { return pipeline; }
    operator vk::PipelineLayout() { return layout; }

    vk::PipelineLayout layout;
    vk::Pipeline pipeline;

private:
    entt::registry& reg;
    Context& ctx;
    Renderpass& renderpass;

};

#endif
