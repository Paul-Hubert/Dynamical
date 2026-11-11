#ifndef DY_UI_PASS_H
#define DY_UI_PASS_H

#include "renderer/scene_pass.h"
#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>
#include <imgui/imgui.h>

#include <memory>
#include <vector>

namespace dy {

class Context;

// Dear ImGui, desktop window only
class UIPass : public ScenePass {
public:
    UIPass(Context& ctx, entt::registry& reg);
    ~UIPass() override;

    const char* name() const override { return "UI"; }
    bool supports(RenderTarget target) const override { return target == RenderTarget::Desktop; }
    bool overlay() const override { return true; }
    void createPipeline(RenderTarget target, vk::RenderPass renderpass) override;
    void update(uint32_t frame_index) override;
    void record(vk::CommandBuffer command, const RenderView& view) override;

private:
    Context& ctx;
    entt::registry& reg;

    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;
    vk::DescriptorSet descSet;
    vk::PipelineLayout layout;

    std::shared_ptr<VmaImage> fontAtlas;
    vk::ImageView fontView;
    vk::Sampler fontSampler;

    struct FrameData {
        VmaBuffer vertices;
        VmaBuffer indices;
        void* vertex_pointer = nullptr;
        void* index_pointer = nullptr;
    };
    std::vector<FrameData> frames;

    ImDrawData* draw_data = nullptr;
};

}

#endif
