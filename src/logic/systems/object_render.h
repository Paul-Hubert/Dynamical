#ifndef OBJECT_RENDER_SYS_H
#define OBJECT_RENDER_SYS_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include "imgui.h"

#include "system.h"

#include <entt/entt.hpp>

#include <glm/glm.hpp>

#include <memory>

class Context;
class Renderpass;

class ObjectRenderSys : public System {
public:
    ObjectRenderSys(entt::registry& reg);
    ~ObjectRenderSys() override;
    
    const char* name() override {
        return "ObjectRender";
    }

    void tick(float dt) override;
    
private:
    
    void initPipeline(vk::RenderPass);
    
    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline graphicsPipeline;
    
    struct per_frame {
        VmaBuffer uniformBuffer;
        void* uniformPointer;
        vk::DescriptorSet descSet;
    };
    std::vector<per_frame> per_frame;
    
};

#endif



