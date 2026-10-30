#ifndef DY_TERRAIN_PASS_H
#define DY_TERRAIN_PASS_H

#include "renderer/scene_pass.h"
#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>

#include <memory>
#include <vector>

namespace dy {

class Context;

// Vertex input of the marching cubes meshes, shared with the grass pass
std::vector<vk::VertexInputBindingDescription> terrainVertexBindings();
std::vector<vk::VertexInputAttributeDescription> terrainVertexAttributes();

// Draws the marching cubes chunks with triplanar grass / rock texturing
class TerrainPass : public ScenePass {
public:
    TerrainPass(Context& ctx, entt::registry& reg);
    ~TerrainPass() override;

    const char* name() const override { return "Terrain"; }
    void createPipeline(RenderTarget target, vk::RenderPass renderpass) override;
    void record(vk::CommandBuffer command, const RenderView& view) override;

private:
    Context& ctx;
    entt::registry& reg;

    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout materialLayout;
    vk::DescriptorSet materialSet;
    vk::PipelineLayout layout;

    std::shared_ptr<VmaImage> texture;
    vk::ImageView textureView;
    vk::Sampler sampler;
};

}

#endif
