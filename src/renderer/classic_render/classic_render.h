#ifndef CLASSIC_RENDER_H
#define CLASSIC_RENDER_H

#include <entt/entt.hpp>
#include "renderpass.h"

#include "renderer/util/vmapp.h"
#include "renderer/context/num_frames.h"

#include "renderer/view_ubo.h"

namespace dy {

class Context;
class Camera;

class ClassicRender {
public:
    ClassicRender(Context& ctx, entt::registry& reg);

    void prepare();
    void render(vk::Semaphore semaphore = nullptr);

    vk::RenderPass getRenderpass() {
        return renderpass;
    }

    ~ClassicRender();


    ClRenderpass renderpass;
    
    vk::DescriptorSetLayout view_layout;
    
    vk::CommandBuffer command;
    
    struct per_frame {
        vk::CommandBuffer commandBuffer;
        vk::Fence fence;
        vk::DescriptorSet set;
        VmaBuffer ubo;
        ViewUBO* pointer;
        vk::Semaphore acquireSemaphore;
        vk::Semaphore presentSemaphore;
    };
    std::vector<per_frame> per_frame;
    
private:
    entt::registry& reg;
    Context& ctx;

    vk::CommandPool commandPool;

    vk::DescriptorPool descriptorPool;

};

}

#endif
