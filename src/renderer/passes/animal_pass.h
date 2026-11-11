#ifndef DY_ANIMAL_PASS_H
#define DY_ANIMAL_PASS_H

#include "renderer/scene_pass.h"
#include "renderer/util/texture.h"
#include "renderer/util/vmapp.h"

#include "logic/animals/animal.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dy {

class Context;

// Draws every entity with a Transform, an Animal and an AnimalPose : skinned meshes of the animal library
// (docs/animals.md), four joints a vertex, their matrices in one storage buffer a frame. One level of detail per
// animal and target, animals out of the desktop view are skipped. No shadows.
//
// A species is loaded the first time an animal uses it : files and mip levels on the taskflow executor,
// Vulkan objects on the main thread.
class AnimalPass : public ScenePass {
public:
    AnimalPass(Context& ctx, entt::registry& reg);
    ~AnimalPass() override;

    const char* name() const override { return "Animals"; }
    void createPipeline(RenderTarget target, vk::RenderPass renderpass) override;
    void update(uint32_t frame_index) override;
    void record(vk::CommandBuffer command, const RenderView& view) override;

    static constexpr uint32_t max_joints = 65536;   // skinning matrices a frame

private:

    struct Material {
        vk::DescriptorSet set;
        glm::vec4 tint = glm::vec4(1.f);
        float alpha_cutoff = 0.f;
        bool two_sided = false;
        bool has_normal = false;
    };

    struct Species {
        enum class State { Loading, Ready, Failed } state = State::Loading;
        std::shared_ptr<const AnimalSpecies> def;
        std::shared_ptr<VmaBuffer> vertices;
        std::shared_ptr<VmaBuffer> indices;
        std::vector<Material> materials;
        float height = 1.f;
        glm::vec3 center = glm::vec3(0.f);  // of the sphere around the animal, model space
        float radius = 1.f;
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
        std::shared_ptr<const AnimalSpecies> def;
        std::vector<AnimalVertex> vertices;
        std::vector<uint32_t> indices;
        std::string error;
        std::atomic<bool> done = false;
    };

    struct Instance {
        uint32_t species;
        uint32_t lod;
        uint32_t first_joint;
        glm::mat4 matrix;
    };

    struct Frame {
        VmaBuffer joints;
        glm::mat4* pointer = nullptr;
        vk::DescriptorSet set;
    };

    void request(uint32_t species);
    void finishJob(Job& job);
    vk::ImageView viewOf(const std::shared_ptr<VmaImage>& image);

    Context& ctx;
    entt::registry& reg;

    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout materialLayout;
    vk::DescriptorSetLayout jointLayout;
    vk::PipelineLayout layout;
    vk::Sampler sampler;

    std::array<vk::Pipeline, NUM_RENDER_TARGETS> two_sided_pipelines {};

    std::shared_ptr<VmaImage> white, flat_normal;

    // Textures are shared between animals, keyed by their path in the library
    struct Texture {
        std::shared_ptr<VmaImage> image;
        vk::ImageView view;
    };
    std::unordered_map<std::string, Texture> textures;
    std::unordered_map<VmaImage*, vk::ImageView> default_views;

    std::vector<std::unique_ptr<Species>> species;      // by library index, null until requested
    std::vector<std::shared_ptr<Job>> jobs;
    std::vector<Frame> frames;

    // What each target draws this frame, sorted by species and level
    std::array<std::vector<Instance>, NUM_RENDER_TARGETS> instances;
    uint32_t frame = 0;
};

}

#endif
