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

    void tick(double dt) override;

private:

    void initPipeline();

    // Per-LOD index buffers and index counts
    struct LODLevel {
        int grid_size;
        int numIndices;
        std::shared_ptr<dy::VmaBuffer> indexBuffer;
    };
    LODLevel lod_levels[3]; // LOD 0 (32), LOD 1 (16), LOD 2 (8)

    vk::PipelineLayout pipelineLayout;
    vk::Pipeline graphicsPipeline;

};

}

#endif


