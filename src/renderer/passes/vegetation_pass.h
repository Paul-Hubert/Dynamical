#ifndef DY_VEGETATION_PASS_H
#define DY_VEGETATION_PASS_H

#include "renderer/scene_pass.h"
#include "renderer/util/texture.h"
#include "renderer/util/vmapp.h"

#include "logic/vegetation/vegetation.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dy {

class Context;

// Draws every entity with a Transform and a Plant : SpeedTree geometry of the vegetation library, one level of
// detail per plant and target (the desktop camera and the headset are not at the same place), alpha tested
// leaves lit from both sides, billboard cards in the distance. No wind, no shadows.
//
// A species is loaded the first time a plant uses it : files and mip levels on the taskflow executor,
// Vulkan objects on the main thread.
class VegetationPass : public ScenePass {
public:
    VegetationPass(Context& ctx, entt::registry& reg);
    ~VegetationPass() override;

    const char* name() const override { return "Vegetation"; }
    void createPipeline(RenderTarget target, vk::RenderPass renderpass) override;
    void update(uint32_t frame_index) override;
    void record(vk::CommandBuffer command, const RenderView& view) override;

private:

    struct Material {
        vk::DescriptorSet set;
        glm::vec4 tint = glm::vec4(1.f);
        bool two_sided = false;
        bool billboard = false;
        bool has_normal = false;
        bool has_subsurface = false;
    };

    struct Species {
        enum class State { Loading, Ready, Failed } state = State::Loading;
        VegetationSpeciesDef def;
        std::shared_ptr<VmaBuffer> vertices;
        std::shared_ptr<VmaBuffer> indices;
        std::vector<Material> materials;
        float height = 1.f;
        float radius = 1.f;     // of the sphere around the plant, centred at half its height
    };

    // CPU side of a species, filled on a worker thread
    struct Job {
        uint32_t species = 0;
        struct Texture {
            std::string path;
            bool srgb = false;
            float alpha_cutoff = 0.f;
            TexturePixels pixels;
            bool failed = false;
        };
        std::vector<Texture> textures;
        VegetationSpeciesDef def;
        std::vector<VegetationVertex> vertices;
        std::vector<uint32_t> indices;
        std::string error;
        std::atomic<bool> done = false;
    };

    struct Instance {
        uint32_t species;
        uint32_t lod;
        glm::mat4 matrix;
    };

    void request(uint32_t species);
    void finishJob(Job& job);
    vk::ImageView textureView(const std::string& path, const std::shared_ptr<VmaImage>& fallback);
    vk::ImageView viewOf(const std::shared_ptr<VmaImage>& image);

    Context& ctx;
    entt::registry& reg;

    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout materialLayout;
    vk::PipelineLayout layout;
    vk::Sampler sampler;
    // Leaf atlases : the smallest mip levels blend leaves with the twigs next to them and with the dilated
    // background, a crown turns brown in the distance. These textures stop at 1/32 of their size
    vk::Sampler atlas_sampler;

    // Leaves are drawn from both sides
    std::array<vk::Pipeline, NUM_RENDER_TARGETS> two_sided_pipelines {};

    std::shared_ptr<VmaImage> white, flat_normal, default_extra, black;

    // Textures are shared between species (the same bark on five pines), keyed by their path in the library
    struct Texture {
        std::shared_ptr<VmaImage> image;
        vk::ImageView view;
        bool cutout = false;
        glm::vec3 mean = glm::vec3(0.4f);   // colour of the whole texture, linear
    };
    std::unordered_map<std::string, Texture> textures;
    std::unordered_map<VmaImage*, vk::ImageView> default_views;

    std::vector<std::unique_ptr<Species>> species;      // by library index, null until requested
    std::vector<std::shared_ptr<Job>> jobs;

    // What each target draws this frame, sorted by species and level
    std::array<std::vector<Instance>, NUM_RENDER_TARGETS> instances;
};

}

#endif
