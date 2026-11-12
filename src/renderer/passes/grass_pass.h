#ifndef DY_GRASS_PASS_H
#define DY_GRASS_PASS_H

#include "renderer/scene_pass.h"
#include "renderer/terrain/chunk_layers.h"
#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace dy {

class Context;
class GrassTopField;

// Grass without blade geometry : the terrain chunks are drawn again as a shell at the top of the grass,
// and each pixel looks its view ray up in rays baked against a grass model (logic/grass/grass_bake.h)
class GrassPass : public ScenePass {
public:
    GrassPass(Context& ctx, entt::registry& reg);
    ~GrassPass() override;

    const char* name() const override { return "Grass"; }
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

    // Blades are seen through where the view ray comes out of the grass again, over a crest : where the
    // shell is seen from behind in front of the ground (docs/grass/13-see-through.md). With a stencil
    // (Context::sceneStencil) the shell is drawn three times : its back faces mark those pixels (backs),
    // its front faces are drawn whole on the others (pipelines) and seen through on these (cuts).
    // Without one, whole everywhere
    std::array<vk::Pipeline, NUM_RENDER_TARGETS> backs {}, cuts {};

    vk::Sampler sampler;
    vk::Sampler raySampler;     // trilinear and anisotropic, for the baked rays

    // Oblique rays (x, z of the tile, and a layer for each azimuth of each elevation : GPUs do not filter
    // 3D images anisotropically, the shader blends the layers) and rays from the top (2D), four textures
    // each (logic/grass/grass_textures.h) : height of the hit, normal, id of the blade, colour
    struct Baked {
        std::shared_ptr<VmaImage> image;
        vk::ImageView view;
    };
    std::array<Baked, 8> baked;
    // Specialization constants of grass.frag.glsl : how the layers are laid out
    struct RayLayout {
        int32_t angles[4] = {};         // azimuths of each elevation
        float low_from = 1.f, low_to = 0.5f;    // of the lowest rays (GrassRaycast)
        float tiles[10] = {}, heights[10] = {}; // metres each kind of tile was modelled at (GrassModel)
        float abundance[10] = {};               // GrassKindInfo
        int32_t small_mask = 0;                 // a bit per kind : small plants, fewer in the shade of tall grass
        int32_t forced_kind = -1;               // grass_kind=... : that kind everywhere
    } rayLayout;

    std::shared_ptr<VmaImage> noiseImage;
    vk::ImageView noiseView;

    // Noises the kinds of plants are chosen by (logic/grass/grass_species.h)
    std::shared_ptr<VmaImage> speciesImage;
    vk::ImageView speciesView;

    // Top of the plants over each chunk (logic/grass/grass_species.h) : one layer of a 2D array per
    // chunk, baked when the chunk gets its mesh and again when the parameters change
    std::unique_ptr<GrassTopField> topField;
    vk::Sampler heightSampler;
    std::shared_ptr<VmaImage> heightImage;
    vk::ImageView heightView;

    ChunkLayers heightLayers;               // which layer each chunk has
};

}

#endif
