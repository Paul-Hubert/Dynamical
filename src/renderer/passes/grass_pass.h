#ifndef DY_GRASS_PASS_H
#define DY_GRASS_PASS_H

#include "renderer/scene_pass.h"
#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>

#include <memory>

namespace dy {

class Context;

// Grass without blade geometry : the terrain chunks are drawn again as a shell at the top of the grass,
// and each pixel looks its view ray up in rays baked against a grass model (logic/grass/grass_bake.h)
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

    // Rays at 45 degrees (3D : x, z of the tile and azimuth) and from the top (2D)
    std::shared_ptr<VmaImage> obliqueImage;
    vk::ImageView obliqueView;
    std::shared_ptr<VmaImage> topImage;
    vk::ImageView topView;

    std::shared_ptr<VmaImage> noiseImage;
    vk::ImageView noiseView;
};

}

#endif
