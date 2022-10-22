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
    
    void initPipeline(vk::RenderPass);
    
    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;
    vk::DescriptorSet descSet;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline graphicsPipeline;
    
    struct per_frame {
        VmaBuffer stagingBuffer;
        void* stagingPointer;
    };
    std::vector<per_frame> per_frame;
    
    struct StoredChunk {
        glm::ivec2 position; // in chunk space
        bool used = false;
        bool stored = false;
        Chunk* chunk;
    };
    std::vector<StoredChunk> stored_chunks;
    int storage_counter = 0;
    
    VmaBuffer storageBuffer;
    
};

}

#endif


