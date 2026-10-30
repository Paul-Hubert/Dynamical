#ifndef DY_MODEL_PASS_H
#define DY_MODEL_PASS_H

#include "renderer/scene_pass.h"
#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>

#include <memory>
#include <unordered_map>

namespace dy {

class Context;

// Draws every entity with a Transform and a ModelRenderable (glTF meshes)
class ModelPass : public ScenePass {
public:
    ModelPass(Context& ctx, entt::registry& reg);
    ~ModelPass() override;

    const char* name() const override { return "Models"; }
    void createPipeline(RenderTarget target, vk::RenderPass renderpass) override;
    void update(uint32_t frame_index) override;
    void record(vk::CommandBuffer command, const RenderView& view) override;

private:
    vk::DescriptorSet makeMaterialSet(const std::shared_ptr<VmaImage>& image);

    Context& ctx;
    entt::registry& reg;

    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout materialLayout;
    vk::PipelineLayout layout;
    vk::Sampler sampler;

    std::shared_ptr<VmaImage> white;

    // One view per texture, shared by the materials using it
    std::unordered_map<VmaImage*, std::pair<std::shared_ptr<VmaImage>, vk::ImageView>> views;
};

}

#endif
