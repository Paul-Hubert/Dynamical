#ifndef BUILDING_RENDER_SYS_H
#define BUILDING_RENDER_SYS_H

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

class BuildingRenderSys : public System {
public:
    BuildingRenderSys(entt::registry& reg);
    ~BuildingRenderSys() override;

    const char* name() override {
        return "BuildingRender";
    }

    void tick(double dt) override;

private:

    void initPipeline();

    vk::PipelineLayout pipelineLayout;
    vk::Pipeline buildingPipeline;

};

}

#endif
