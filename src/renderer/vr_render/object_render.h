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
    ObjectRender(entt::registry& reg, Context& ctx, Renderpass& renderpass, ViewUBO& ubo);
    void render(vk::CommandBuffer command, vk::DescriptorSet set);
    ~ObjectRender();

    void createPipeline(ViewUBO& ubo);

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
