#ifndef MAP_RENDER_SYS_H
#define MAP_RENDER_SYS_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include <imgui/imgui.h>

#include "system.h"

#include <entt/entt.hpp>

#include <glm/glm.hpp>

#include "logic/map/chunk.h"

#include <memory>

namespace dy {

class Context;
class Renderpass;

class MapRenderSys : public System {
public:
    MapRenderSys(entt::registry& reg);
    ~MapRenderSys() override;
    
    const char* name() override {
        return "MapRender";
    }

    void tick(float dt) override;
    
private:
    
    void initPipeline();

    int numIndices;
    std::shared_ptr<dy::VmaBuffer> indexBuffer;

    vk::PipelineLayout pipelineLayout;
    vk::Pipeline graphicsPipeline;
    
};

}

#endif


