#ifndef DY_SKY_PASS_H
#define DY_SKY_PASS_H

#include "renderer/scene_pass.h"

#include <entt/entt.hpp>

namespace dy {

class Context;

// The sky behind everything : skyColor of the shaders, the discs of the sun and of the moon, stars at
// night. Drawn first by every renderer, scenes do not list it
class SkyPass : public ScenePass {
public:
    SkyPass(Context& ctx, entt::registry& reg);
    ~SkyPass() override;

    const char* name() const override { return "Sky"; }
    void createPipeline(RenderTarget target, vk::RenderPass renderpass) override;
    void record(vk::CommandBuffer command, const RenderView& view) override;

private:
    Context& ctx;
    entt::registry& reg;
    vk::PipelineLayout layout;
};

}

#endif
