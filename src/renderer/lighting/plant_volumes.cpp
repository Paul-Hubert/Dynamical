#include "plant_volumes.h"

#include <algorithm>
#include <cmath>

#include "renderer/context/context.h"
#include "renderer/util/pipeline.h"

#include "logic/components/lighting_stats.h"
#include "logic/vegetation/vegetation.h"

#include "util/log.h"

using namespace dy;

namespace {

const glm::ivec3 most_texels(64, 128, 64);
constexpr int slices_a_frame = 8;
constexpr uint32_t most_levels = 4;
const vk::Format format = vk::Format::eR8G8B8A8Unorm;

// Where the species stands while its volume is baked : far under the world, only the rays of the bake see it
const glm::vec3 bake_origin(0.f, -20000.f, 0.f);

// Push constants of plant_bake.rt.comp.glsl
struct BakePC {
    glm::vec4 origin;
    glm::vec4 low;
    glm::ivec4 size;
    glm::vec4 heart;
    glm::vec4 fill;
};

}

PlantVolumes::PlantVolumes(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    Device& device = ctx.device;

    reg.ctx().emplace<PlantVolumeSet>();

    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    for(uint32_t i = 0; i < 3; i++) bindings.push_back(vk::DescriptorSetLayoutBinding(i, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute));
    images_layout = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));

    auto sizes = std::vector { vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, max_volumes * 3) };
    pool = device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, max_volumes, sizes));

    auto layouts = std::vector { ctx.view_layout, images_layout };
    auto range = vk::PushConstantRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(BakePC));
    layout = device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, range));

    vk::ShaderModule shader = loadShader(device, "./resources/shaders/plant_bake.rt.comp.glsl.spv");
    pipeline = device->createComputePipeline(nullptr, vk::ComputePipelineCreateInfo({},
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, shader, "main"), layout)).value;
    device->destroy(shader);

    vk::SamplerCreateInfo info({}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge);
    info.maxLod = VK_LOD_CLAMP_NONE;
    sampler = device->createSampler(info);

    ctx.plant_volumes = &infos;

}

// The textures of a species, empty, and what its bake needs. false : the species gets no volume
bool PlantVolumes::start(uint32_t species) {

    Device& device = ctx.device;
    const auto& plants = reg.ctx().get<RayPlantMeshes>();
    const auto& given = reg.ctx().get<RayMeshes>();

    glm::vec3 low = plants.min[species], high = plants.max[species];
    glm::vec3 extent = high - low;
    if(extent.y < min_height || volumes.size() >= max_volumes) return false;

    auto volume = std::make_unique<Volume>();
    volume->species = species;

    // The same texel along the three axes, as small as the largest volume allows
    float voxel = std::max({ extent.x / (float) most_texels.x, extent.y / (float) most_texels.y, extent.z / (float) most_texels.z, 0.03f });
    volume->size = glm::clamp(glm::ivec3(glm::ceil(extent / voxel)), glm::ivec3(4), most_texels);
    volume->bounds.min = low;
    volume->bounds.size = glm::vec3(volume->size) * voxel;
    volume->bounds.voxel = voxel;
    int smallest = std::min({ volume->size.x, volume->size.y, volume->size.z });
    volume->levels = std::min<uint32_t>(most_levels, (uint32_t) std::floor(std::log2((float) smallest)) + 1);

    // Where nothing is, the mean colour of the leaves of the species : filtering reads those texels too
    {
        glm::vec3 colour(0.f);
        float leaves = 0.f, parts = 0.f;
        glm::vec3 any(0.f);
        for(auto& part : given.meshes[plants.mesh[species]].parts) {
            any += part.color;
            parts += 1.f;
            if(part.opaque) continue;
            colour += part.color;
            leaves += 1.f;
        }
        if(leaves > 0.f) volume->fill = glm::vec4(colour / leaves, 1.f);
        else if(parts > 0.f) volume->fill = glm::vec4(any / parts, 0.f);
    }

    vk::CommandBuffer setup = ctx.transfer.getCommandBuffer();
    vk::Extent3D extent3(volume->size.x, volume->size.y, volume->size.z);
    for(int i = 0; i < 3; i++) {
        VmaAllocationCreateInfo alloc {};
        alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        volume->images[i] = VmaImage(device, &alloc, vk::ImageCreateInfo({}, vk::ImageType::e3D, format, extent3, volume->levels, 1, vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
            vk::SharingMode::eExclusive, 1, &device.g_i, vk::ImageLayout::eUndefined));
        vk::ImageSubresourceRange all(vk::ImageAspectFlagBits::eColor, 0, volume->levels, 0, 1);
        volume->views[i] = device->createImageView(vk::ImageViewCreateInfo({}, volume->images[i].image, vk::ImageViewType::e3D, format, vk::ComponentMapping(), all));
        volume->first_level[i] = device->createImageView(vk::ImageViewCreateInfo({}, volume->images[i].image, vk::ImageViewType::e3D, format, vk::ComponentMapping(),
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
        // Written by the bake, then read by everything : one layout
        setup.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eAllCommands, {}, {}, {},
            vk::ImageMemoryBarrier({}, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, volume->images[i].image, all));
    }

    volume->set = device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &images_layout))[0];
    std::array<vk::DescriptorImageInfo, 3> images;
    std::vector<vk::WriteDescriptorSet> writes;
    for(uint32_t i = 0; i < 3; i++) {
        images[i] = vk::DescriptorImageInfo({}, volume->first_level[i], vk::ImageLayout::eGeneral);
        writes.push_back(vk::WriteDescriptorSet(volume->set, i, 0, 1, vk::DescriptorType::eStorageImage, &images[i]));
    }
    device->updateDescriptorSets(writes, {});

    baking = std::move(volume);
    return true;

}

void PlantVolumes::prepare() {

    auto* plants = reg.ctx().find<RayPlantMeshes>();
    auto& bake = reg.ctx().get<RayBake>();
    pending = 0;
    if(!plants) return;

    done.resize(plants->mesh.size(), 0);

    // The next species that has a mesh in the rays and no volume
    uint32_t waiting = 0;
    for(uint32_t species = 0; species < plants->mesh.size(); species++) {
        if(done[species] || plants->mesh[species] < 0 || species >= plants->min.size()) continue;
        if(baking && baking->species == species) continue;
        if(!baking && !start(species)) {
            done[species] = 1;
            continue;
        }
        if(!(baking && baking->species == species)) waiting++;
    }
    pending = waiting + (baking ? 1 : 0);

    bake.mesh = baking ? plants->mesh[baking->species] : -1;
    bake.origin = bake_origin;

}

void PlantVolumes::update(vk::DescriptorSet view_set) {

    // Species still to bake : what a capture waits for
    reg.ctx().get<LightingStats>().structures_pending += pending;

    if(!baking || !reg.ctx().get<RayBake>().placed) return;

    Volume& volume = *baking;
    vk::CommandBuffer command = ctx.compute.getCommandBuffer();

    int slices = std::min(slices_a_frame, volume.size.z - volume.next_slice);

    BakePC pc;
    pc.origin = glm::vec4(bake_origin, volume.bounds.voxel);
    pc.low = glm::vec4(volume.bounds.min, 0.f);
    pc.size = glm::ivec4(volume.size, volume.next_slice);
    pc.heart = glm::vec4(volume.bounds.min + volume.bounds.size * glm::vec3(0.5f, 0.55f, 0.5f), (float) slices);
    pc.fill = volume.fill;

    command.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
    command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, layout, 0, { view_set, volume.set }, {});
    command.pushConstants(layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(BakePC), &pc);
    command.dispatch((volume.size.x + 3) / 4, (volume.size.y + 3) / 4, (slices + 3) / 4);
    command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eAllCommands, {},
        vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite), {}, {});

    volume.next_slice += slices;
    if(volume.next_slice >= volume.size.z) finish(command);

}

// The smaller levels, and the volume is given to the rays and to the pass that draws the plants
void PlantVolumes::finish(vk::CommandBuffer command) {

    Volume& volume = *baking;

    for(auto& image : volume.images) {
        glm::ivec3 from = volume.size;
        for(uint32_t level = 1; level < volume.levels; level++) {
            glm::ivec3 to = glm::max(from / 2, glm::ivec3(1));
            vk::ImageBlit blit(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level - 1, 0, 1),
                { vk::Offset3D(0, 0, 0), vk::Offset3D(from.x, from.y, from.z) },
                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level, 0, 1),
                { vk::Offset3D(0, 0, 0), vk::Offset3D(to.x, to.y, to.z) });
            command.blitImage(image.image, vk::ImageLayout::eGeneral, image.image, vk::ImageLayout::eGeneral, blit, vk::Filter::eLinear);
            command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands, {},
                vk::MemoryBarrier(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite), {}, {});
            from = to;
        }
    }

    volume.bounds.volume = (int32_t) volumes.size();
    for(int i = 0; i < 3; i++) infos[i].push_back(vk::DescriptorImageInfo(sampler, volume.views[i], vk::ImageLayout::eGeneral));

    auto& plants = reg.ctx().get<RayPlantMeshes>();
    if(plants.box.size() < plants.mesh.size()) plants.box.resize(plants.mesh.size(), -1);
    plants.box[volume.species] = (int32_t) reg.ctx().get<RayMeshes>().add(volume.bounds);

    auto& set = reg.ctx().get<PlantVolumeSet>();
    if(set.species.size() <= volume.species) set.species.resize(plants.mesh.size());
    set.species[volume.species] = { volume.bounds.volume, volume.bounds.min, volume.bounds.size, volume.bounds.voxel };

    if(auto* library = reg.ctx().find<VegetationLibrary>(); library && volume.species < library->species.size()) {
        dy::log(dy::info) << "Lighting : volume of " << library->species[volume.species].id << ", " << volume.size.x << " x " << volume.size.y << " x " << volume.size.z
            << " texels of " << volume.bounds.voxel * 100.f << " cm\n";
    }

    done[volume.species] = 1;
    volumes.push_back(std::move(baking));

}

PlantVolumes::~PlantVolumes() {

    Device& device = ctx.device;
    device->waitIdle();

    ctx.plant_volumes = nullptr;
    reg.ctx().erase<PlantVolumeSet>();

    if(baking) volumes.push_back(std::move(baking));
    for(auto& volume : volumes) {
        for(auto view : volume->views) device->destroy(view);
        for(auto view : volume->first_level) device->destroy(view);
    }
    device->destroy(sampler);
    device->destroy(pipeline);
    device->destroy(layout);
    device->destroy(pool);
    device->destroy(images_layout);

}
