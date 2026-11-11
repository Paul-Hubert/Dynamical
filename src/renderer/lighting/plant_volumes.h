#ifndef DY_PLANT_VOLUMES_H
#define DY_PLANT_VOLUMES_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"
#include "renderer/lighting/ray_meshes.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <vector>

namespace dy {

class Context;

// The baked volumes of the plants (docs/lighting.md, "Plant volumes") : per species, three 3D textures over
// the bounds of its most detailed level, at most 64 x 128 x 64 texels with their smaller levels :
//  - density : rgb how much of the light a texel of the plant stops, per colour, a : how likely a ray is
//    to meet something across it
//  - colour : rgb mean colour of what is there, a : how much of it is leaf
//  - normal : rg mean normal (octahedral), b : how much of the sky the texel sees through its own plant
// Baked on the GPU, a few slices a frame, once the acceleration structure of the species is built : the
// species is put on its own in the scene of the frame (RayBake) and plant_bake.rt.comp.glsl traces it.
// Not kept on disk.
//
// Far from the viewers a plant is then a box in the rays, which read its volume (RayBox), and on screen
// VegetationPass draws it from the same textures in place of its billboard (PlantVolumeSet).
// Only exists with ray tracing.
class PlantVolumes {
public:
    PlantVolumes(Context& ctx, entt::registry& reg);
    ~PlantVolumes();

    // Main thread. prepare() before RayScene::update() : asks for the species being baked to be in the
    // scene of the frame. update() after it : records the bake of some of its slices.
    // view_set : a view set of the frame (the one the probes use)
    void prepare();
    void update(vk::DescriptorSet view_set);

    static constexpr uint32_t max_volumes = 128;
    static constexpr float min_height = 2.f;        // plants smaller than that have no volume : a billboard far away, no shadow

private:
    struct Volume {
        uint32_t species = 0;
        RayBox bounds;
        glm::ivec3 size = glm::ivec3(1);
        uint32_t levels = 1;
        std::array<VmaImage, 3> images;             // density, colour, normal
        std::array<vk::ImageView, 3> views;         // every level, to sample
        std::array<vk::ImageView, 3> first_level;   // to write
        vk::DescriptorSet set;
        glm::vec4 fill = glm::vec4(0.2f, 0.3f, 0.1f, 1.f);
        int next_slice = 0;
    };

    bool start(uint32_t species);
    void finish(vk::CommandBuffer command);

    Context& ctx;
    entt::registry& reg;

    vk::DescriptorPool pool;
    vk::DescriptorSetLayout images_layout;
    vk::PipelineLayout layout;
    vk::Pipeline pipeline;
    vk::Sampler sampler;

    std::unique_ptr<Volume> baking;
    std::vector<std::unique_ptr<Volume>> volumes;
    std::vector<uint8_t> done;                      // by species : baked, or never to be
    uint32_t pending = 0;                           // species still to bake
    std::array<std::vector<vk::DescriptorImageInfo>, 3> infos;
};

}

#endif
