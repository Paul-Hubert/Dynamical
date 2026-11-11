#ifndef DY_TERRAIN_PASS_H
#define DY_TERRAIN_PASS_H

#include "renderer/scene_pass.h"
#include "renderer/util/texture.h"
#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <vector>

namespace dy {

class Context;

// Vertex input of the marching cubes meshes, shared with the grass pass
std::vector<vk::VertexInputBindingDescription> terrainVertexBindings();
std::vector<vk::VertexInputAttributeDescription> terrainVertexAttributes();

// Draws the marching cubes chunks : earth and the patches of the soil maps (mud, stones, sand) seen from
// above, rock on the steep faces, each layer coming through the others by its height (terrain.frag.glsl,
// docs/ground.md)
class TerrainPass : public ScenePass {
public:
    TerrainPass(Context& ctx, entt::registry& reg);
    ~TerrainPass() override;

    const char* name() const override { return "Terrain"; }
    void createPipeline(RenderTarget target, vk::RenderPass renderpass) override;
    void record(vk::CommandBuffer command, const RenderView& view) override;

private:
    // The textures of resources/ground (tools/ground_import.py), or flat colours when they are not there
    void loadGround();
    // The rays baked on the height maps of the mud, the stones and the sand (logic/terrain/ground_relief.h);
    // without the textures (null) : a flat texel
    void loadRelief(const std::vector<TexturePixels>* details);

    Context& ctx;
    entt::registry& reg;

    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout materialLayout;
    vk::DescriptorSet materialSet;
    vk::PipelineLayout layout;

    std::shared_ptr<VmaImage> texture;
    vk::ImageView textureView;
    vk::Sampler sampler;

    // A layer each : mud, stones, sand, rock, earth. Albedo, and normal + height + occlusion
    std::shared_ptr<VmaImage> groundColor;
    vk::ImageView groundColorView;
    std::shared_ptr<VmaImage> groundDetail;
    vk::ImageView groundDetailView;
    vk::Sampler groundSampler;
    std::array<float, 5> groundTiles { 0.5f, 0.55f, 0.4f, 0.2f, 0.4f };    // tiles per metre
    std::array<float, 5> groundSmoothness { 0.64f, 0.3f, 0.12f, 0.2f, 0.1f };
    bool groundLibrary = false;

    // Heights of the hits of the baked rays, the layers of the three kinds of soil one after the other,
    // and of the rays from the top (a layer each)
    std::shared_ptr<VmaImage> relief, reliefTop;
    vk::ImageView reliefView, reliefTopView;
    bool reliefBaked = false;
    // Specialization constants of terrain.frag.glsl
    struct ReliefLayout {
        int32_t angles[4] = {1, 1, 1, 1};
        float low_from = 1.f, low_to = 0.5f;
    } reliefLayout;
};

}

#endif
