#ifndef VR_RENDER_H
#define VR_RENDER_H

#include <vector>
#include <taskflow/taskflow.hpp>
#include <entt/entt.hpp>

#include "renderpass.h"

#include "renderer/util/vmapp.h"
#include "renderer/context/num_frames.h"

#include "renderer/util/xr.h"
#include "renderer/view_ubo.h"

class Context;
class Camera;

class VRRender {
public:
    VRRender(entt::registry& reg, Context& ctx, vk::DescriptorSetLayout set_layout);

    void record(uint32_t frame_index, vk::CommandBuffer command, std::function<void(vk::CommandBuffer)>& recorder, vk::PipelineLayout pipeline_layout);
    void prepare(uint32_t frame_index, std::function<void(vk::CommandBuffer)>& recorder, vk::PipelineLayout pipeline_layout);
    void render(uint32_t frame_index, std::vector<vk::Semaphore> waits, std::vector<vk::Semaphore> signals);

    vk::RenderPass getRenderpass() {
        return renderpass;
    }

    ~VRRender();
    
private:
    entt::registry& reg;
    Context& ctx;
    Renderpass renderpass;

    vk::CommandPool commandPool;

    vk::DescriptorPool descriptorPool;
    
    std::vector<uint32_t> swapchain_image_indices;

    struct per_frame {
        vk::CommandBuffer commandBuffer;
        vk::Fence fence;
        struct per_ubo {
            vk::DescriptorSet set;
            VmaBuffer ubo;
            ViewUBO* pointer;
        };
        std::vector<per_ubo> ubos;

    };
    std::vector<per_frame> per_frame;

};

#endif
