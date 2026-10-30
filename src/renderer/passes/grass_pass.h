#ifndef DY_GRASS_PASS_H
#define DY_GRASS_PASS_H

#include "renderer/scene_pass.h"
#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>

#include <memory>

namespace dy {

class Context;

// Raymarched grass shells drawn over the terrain chunks (the 2019 grass, re-enabled)
class GrassPass : public ScenePass {
public:
    GrassPass(Context& ctx, entt::registry& reg);
    ~GrassPass() override;

    const char* name() const override { return "Grass"; }
    void createPipeline(RenderTarget target, vk::RenderPass renderpass) override;
    void record(vk::CommandBuffer command, const RenderView& view) override;

private:
    Context& ctx;
    entt::registry& reg;

    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;
    vk::DescriptorSet descSet;
    vk::PipelineLayout layout;

    vk::Sampler sampler;

    std::shared_ptr<VmaImage> raycastImage;
    vk::ImageView raycastView;

    std::shared_ptr<VmaImage> noiseImage;
    vk::ImageView noiseView;
};

}

#endif
