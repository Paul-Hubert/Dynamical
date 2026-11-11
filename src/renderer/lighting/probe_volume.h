#ifndef DY_PROBE_VOLUME_H
#define DY_PROBE_VOLUME_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#include "logic/lighting/probes.h"

namespace dy {

class Context;

// The irradiance probes (docs/lighting.md, "Probes") : a brick of 65³ probes per chunk with a surface, one
// every 4 m, in pages of 8 bricks. Per probe, the light arriving from all around as two bands of spherical
// harmonics per colour (three RGBA16F textures) and how much of the sun and of the sky it sees (RGBA8).
// The scene shaders read them through the view set (include/probes.glsl) : the light of the sky and what
// bounces, and the sun's shadows beyond the traced range.
//
// Every frame a ray budget is spent on the probes that are traced (in the air near the ground or a plant),
// in the compute command buffer of the frame, after the scene the rays see was built : new bricks first,
// then the bricks in turn, the near ones more often. Nothing is kept between runs.
// Only exists with ray tracing.
class ProbeVolume {
public:
    ProbeVolume(Context& ctx, entt::registry& reg);
    ~ProbeVolume();

    // Main thread, after RayScene::update(), before the compute commands of the frame are submitted
    void update(float time);

    // The view set update() wrote for this frame, for the other compute passes of the lighting
    vk::DescriptorSet viewSet() const;

    static constexpr int max_pages = 8;

private:
    struct Page {
        std::array<VmaImage, 3> light;
        VmaImage direct;
        std::shared_ptr<VmaImage> classes;
        std::array<vk::ImageView, 3> light_views;
        vk::ImageView direct_view, classes_view;
        vk::DescriptorSet set;
    };

    struct Brick {
        bool used = false;
        entt::entity chunk = entt::null;
        glm::ivec3 position = glm::ivec3(0);
        ProbeClasses classes;
        size_t active_cursor = 0, fill_cursor = 0;
        uint64_t sweeps = 0;        // times all its probes were traced
        bool fresh = true;          // its first values are still to write
        double turn = 0.;           // when its next share of the rays is due, in the time of the schedule
        std::unordered_map<uint32_t, uint8_t> hot;  // probes near a torch : frames they are still traced every frame
    };

    void createPage();
    void reserveFound(uint32_t rays);
    void syncBricks(glm::vec3 viewer);
    void uploadIndex();

    Context& ctx;
    entt::registry& reg;

    ProbeBricks bricks;
    std::vector<Brick> brick_data;
    std::vector<std::unique_ptr<Page>> pages;

    std::shared_ptr<VmaImage> index_image;
    vk::ImageView index_view;
    vk::Sampler index_sampler, light_sampler;
    std::vector<vk::DescriptorImageInfo> light_infos, direct_infos;

    vk::DescriptorPool pool;
    vk::DescriptorSetLayout page_layout, work_layout;
    vk::PipelineLayout layout;
    vk::Pipeline pipeline;

    // Per frame in flight : the view set of the dispatches and the lists of probes
    struct Ring {
        vk::DescriptorSet view_set, work_set;
        VmaBuffer view_buffer, work_buffer;
        void* view_pointer = nullptr;
        bool written = false;
        uint32_t* work_pointer = nullptr;
    };
    std::vector<Ring> rings;

    // What the rays of a dispatch find, before its probes gather them
    VmaBuffer found;
    uint32_t found_capacity = 0;

    uint64_t frames = 0;
    size_t open_cursor = 0;
    double schedule = 0.;
};

}

#endif
