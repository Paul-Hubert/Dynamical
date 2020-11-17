#ifndef OBJECT_RENDER_H
#define OBJECT_RENDER_H

#include "renderer/vk.h"

#include "renderer/vmapp.h"

#include "glm/glm.hpp"

#include "renderer/num_frames.h"

#include "entt/entt.hpp"

class Context;
class Renderpass;
class UBODescriptor;

struct Vert {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

class ObjectRender {
public:
    ObjectRender(entt::registry& reg, Context& ctx, Renderpass& renderpass, UBODescriptor& ubo);
    void render(vk::CommandBuffer command, uint32_t i);
    ~ObjectRender();

    void createPipeline(UBODescriptor& ubo);

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
