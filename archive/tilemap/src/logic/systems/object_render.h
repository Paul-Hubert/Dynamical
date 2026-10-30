#ifndef OBJECT_RENDER_SYS_H
#define OBJECT_RENDER_SYS_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include <imgui/imgui.h>

#include "system.h"

#include <entt/entt.hpp>

#include <glm/glm.hpp>

#include <memory>

namespace dy {
    
class Context;
class Renderpass;

class ObjectRenderSys : public System {
public:
    ObjectRenderSys(entt::registry& reg);
    ~ObjectRenderSys() override;
    
    const char* name() override {
        return "ObjectRender";
    }

    void tick(double dt) override;
    
private:
    
    void initPipeline();

    vk::PipelineLayout pipelineLayout;
    vk::Pipeline objectPipeline;
    vk::Pipeline particlePipeline;
    
};

}

#endif



