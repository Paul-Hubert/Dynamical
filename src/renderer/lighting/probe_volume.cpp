#include "probe_volume.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"
#include "renderer/lighting/ray_scene.h"
#include "renderer/tree_shade.h"
#include "renderer/util/pipeline.h"
#include "renderer/view_ubo.h"

#include "logic/components/camera.h"
#include "logic/components/lighting_stats.h"
#include "logic/components/point_light.h"
#include "logic/components/render_settings.h"
#include "logic/components/transform.h"
#include "logic/components/vr.h"
#include "logic/terrain/chunk.h"
#include "logic/vegetation/vegetation.h"

#include "util/log.h"

using namespace dy;

namespace {

constexpr uint32_t page_texels = ProbeBricks::page_side * probes::size;
constexpr uint32_t work_capacity = 1u << 18;        // probes listed in a frame
constexpr float keep_light = 0.85f;                 // part of what a probe held that stays when it is traced again
constexpr float keep_hot = 0.7f;                    // the same near a torch
constexpr uint8_t hot_frames = 30;                  // a probe a torch has left is still traced every frame for that long

const vk::Format light_format = vk::Format::eR16G16B16A16Sfloat;
const vk::Format direct_format = vk::Format::eR8G8B8A8Unorm;
const vk::Format classes_format = vk::Format::eR8Uint;
const vk::Format index_format = vk::Format::eR16Uint;

// Push constants of probe_update.rt.comp.glsl
struct ProbePC {
    glm::vec4 origin;
    glm::ivec4 atlas;
    uint32_t mode;
    uint32_t first;
    uint32_t count;
    uint32_t rays;
    float keep;
    uint32_t seed;
};

enum Mode : uint32_t { First = 0, Trace = 1, Fill = 2, Open = 3, Gather = 4 };

constexpr uint32_t sun_rays = 4;    // of probe_update.rt.comp.glsl

}

ProbeVolume::ProbeVolume(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg), bricks(max_pages), rings(NUM_FRAMES) {

    Device& device = ctx.device;

    // The table of the bricks : empty
    {
        std::vector<uint16_t> empty((size_t) ProbeBricks::wrap * ProbeBricks::wrap * ProbeBricks::wrap, 0);
        vk::ImageCreateInfo info({}, vk::ImageType::e3D, index_format, vk::Extent3D(ProbeBricks::wrap, ProbeBricks::wrap, ProbeBricks::wrap), 1, 1,
            vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::SharingMode::eExclusive, 1, &device.g_i, vk::ImageLayout::eUndefined);
        index_image = ctx.transfer.createImage(empty.data(), empty.size() * sizeof(uint16_t), info, vk::ImageLayout::eShaderReadOnlyOptimal);
        index_view = device->createImageView(vk::ImageViewCreateInfo({}, index_image->image, vk::ImageViewType::e3D, index_format,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
    }

    index_sampler = device->createSampler(vk::SamplerCreateInfo({}, vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat));
    light_sampler = device->createSampler(vk::SamplerCreateInfo({}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge));

    // Set 1 of the update : the textures of a page. Set 2 : the probes listed this frame
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        for(uint32_t i = 0; i < 5; i++) bindings.push_back(vk::DescriptorSetLayoutBinding(i, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute));
        page_layout = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));

        auto work = std::vector {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
        };
        work_layout = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, work));

        auto sizes = ctx.viewPoolSizes(NUM_FRAMES);
        sizes.push_back(vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, max_pages * 5));
        sizes.push_back(vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 2 * NUM_FRAMES));
        pool = device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, max_pages + 2 * NUM_FRAMES, sizes));
    }

    {
        auto layouts = std::vector { ctx.view_layout, page_layout, work_layout };
        auto range = vk::PushConstantRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(ProbePC));
        layout = device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, range));

        vk::ShaderModule shader = loadShader(device, "./resources/shaders/probe_update.rt.comp.glsl.spv");
        pipeline = device->createComputePipeline(nullptr, vk::ComputePipelineCreateInfo({},
            vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, shader, "main"), layout)).value;
        device->destroy(shader);
    }

    for(auto& ring : rings) {
        VmaAllocationCreateInfo alloc {};
        alloc.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        alloc.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info;

        ring.view_buffer = VmaBuffer(device, &alloc, vk::BufferCreateInfo({}, sizeof(ViewUBO), vk::BufferUsageFlagBits::eUniformBuffer,
            vk::SharingMode::eExclusive, 1, &device.g_i));
        vmaGetAllocationInfo(device, ring.view_buffer.allocation, &info);
        ring.view_pointer = info.pMappedData;

        ring.work_buffer = VmaBuffer(device, &alloc, vk::BufferCreateInfo({}, (vk::DeviceSize) work_capacity * sizeof(uint32_t),
            vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive, 1, &device.g_i));
        vmaGetAllocationInfo(device, ring.work_buffer.allocation, &info);
        ring.work_pointer = static_cast<uint32_t*>(info.pMappedData);

        ring.view_set = device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &ctx.view_layout))[0];
        ring.work_set = device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &work_layout))[0];

        auto view_info = vk::DescriptorBufferInfo(ring.view_buffer, 0, sizeof(ViewUBO));
        auto work_info = vk::DescriptorBufferInfo(ring.work_buffer, 0, VK_WHOLE_SIZE);
        device->updateDescriptorSets({
            vk::WriteDescriptorSet(ring.view_set, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &view_info, nullptr),
            vk::WriteDescriptorSet(ring.work_set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &work_info, nullptr),
        }, {});
    }

    ctx.probe_index_view = index_view;
    ctx.probe_index_sampler = index_sampler;
    ctx.probe_light = &light_infos;
    ctx.probe_direct = &direct_infos;

}

// Room for what the rays of a dispatch find : on the GPU only, each dispatch after the one before
void ProbeVolume::reserveFound(uint32_t rays) {

    if(rays <= found_capacity) return;

    Device& device = ctx.device;
    device->waitIdle();

    found_capacity = std::max(rays, 1u << 16);
    VmaAllocationCreateInfo alloc {};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    found = VmaBuffer(device, &alloc, vk::BufferCreateInfo({}, (vk::DeviceSize) found_capacity * 4 * sizeof(float),
        vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive, 1, &device.g_i));

    auto info = vk::DescriptorBufferInfo(found, 0, VK_WHOLE_SIZE);
    for(auto& ring : rings) {
        device->updateDescriptorSets({
            vk::WriteDescriptorSet(ring.work_set, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &info, nullptr),
        }, {});
    }

}

void ProbeVolume::createPage() {

    Device& device = ctx.device;
    auto page = std::make_unique<Page>();

    vk::Extent3D extent(page_texels, page_texels, page_texels);
    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    vk::CommandBuffer setup = ctx.transfer.getCommandBuffer();

    // Read by the scene shaders while the update writes them, a probe at a time : one layout for both
    auto make = [&](VmaImage& image, vk::ImageView& view, vk::Format format) {
        VmaAllocationCreateInfo alloc {};
        alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        image = VmaImage(device, &alloc, vk::ImageCreateInfo({}, vk::ImageType::e3D, format, extent, 1, 1, vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled, vk::SharingMode::eExclusive, 1, &device.g_i,
            vk::ImageLayout::eUndefined));
        view = device->createImageView(vk::ImageViewCreateInfo({}, image.image, vk::ImageViewType::e3D, format, vk::ComponentMapping(), range));
        setup.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eAllCommands, {}, {}, {},
            vk::ImageMemoryBarrier({}, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image.image, range));
    };
    for(int i = 0; i < 3; i++) make(page->light[i], page->light_views[i], light_format);
    make(page->direct, page->direct_view, direct_format);

    {
        std::vector<uint8_t> rock((size_t) page_texels * page_texels * page_texels, 0);
        vk::ImageCreateInfo info({}, vk::ImageType::e3D, classes_format, extent, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive, 1, &device.g_i, vk::ImageLayout::eUndefined);
        page->classes = ctx.transfer.createImage(rock.data(), rock.size(), info, vk::ImageLayout::eGeneral);
        page->classes_view = device->createImageView(vk::ImageViewCreateInfo({}, page->classes->image, vk::ImageViewType::e3D, classes_format,
            vk::ComponentMapping(), range));
    }

    page->set = device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &page_layout))[0];
    std::array<vk::DescriptorImageInfo, 5> images {
        vk::DescriptorImageInfo({}, page->light_views[0], vk::ImageLayout::eGeneral),
        vk::DescriptorImageInfo({}, page->light_views[1], vk::ImageLayout::eGeneral),
        vk::DescriptorImageInfo({}, page->light_views[2], vk::ImageLayout::eGeneral),
        vk::DescriptorImageInfo({}, page->direct_view, vk::ImageLayout::eGeneral),
        vk::DescriptorImageInfo({}, page->classes_view, vk::ImageLayout::eGeneral),
    };
    std::vector<vk::WriteDescriptorSet> writes;
    for(uint32_t i = 0; i < 5; i++) writes.push_back(vk::WriteDescriptorSet(page->set, i, 0, 1, vk::DescriptorType::eStorageImage, &images[i]));
    device->updateDescriptorSets(writes, {});

    // For the view sets of the frames to come
    for(int i = 0; i < 3; i++) light_infos.push_back(vk::DescriptorImageInfo(light_sampler, page->light_views[i], vk::ImageLayout::eGeneral));
    direct_infos.push_back(vk::DescriptorImageInfo(light_sampler, page->direct_view, vk::ImageLayout::eGeneral));

    pages.push_back(std::move(page));
    dy::log(dy::info) << "Lighting : " << pages.size() << " pages of probes, " << pages.size() * 61 << " MB\n";

}

void ProbeVolume::uploadIndex() {
    const auto& table = bricks.indirection();
    ctx.transfer.updateImage(table.data(), table.size() * sizeof(uint16_t), index_image->image,
        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0),
        vk::Extent3D(ProbeBricks::wrap, ProbeBricks::wrap, ProbeBricks::wrap), vk::ImageLayout::eShaderReadOnlyOptimal);
}

// A brick for the nearest chunk that has none, and none for the chunks that are gone
void ProbeVolume::syncBricks(glm::vec3 viewer) {

    bool changed = false;

    for(auto& brick : brick_data) {
        if(!brick.used) continue;
        if(reg.valid(brick.chunk) && reg.all_of<ChunkLit, ChunkVolume>(brick.chunk) && !reg.all_of<ChunkDestroying>(brick.chunk)) continue;
        bricks.release(brick.position);
        brick = Brick();
        changed = true;
    }

    // Chunks in the rays that have a surface and no brick, the nearest first
    entt::entity nearest = entt::null;
    float nearest_distance = 1e30f;
    uint32_t waiting = 0;
    for(auto [entity, chunk, volume] : reg.view<const ChunkC, const ChunkVolume, const ChunkLit>(entt::exclude<ChunkDestroying>).each()) {
        if(bricks.find(chunk.pos) >= 0) continue;
        waiting++;
        float distance = glm::distance(chunk.center(), viewer);
        if(distance < nearest_distance) {
            nearest_distance = distance;
            nearest = entity;
        }
    }

    if(nearest != entt::null) {
        const ChunkC& chunk = reg.get<ChunkC>(nearest);
        int id = bricks.allocate(chunk.pos);
        if(id < 0) {
            // No room : the furthest brick goes, if it is clearly further
            int furthest = -1;
            float furthest_distance = nearest_distance + (float) chunk::length;
            for(size_t i = 0; i < brick_data.size(); i++) {
                if(!brick_data[i].used) continue;
                float distance = glm::distance((glm::vec3(brick_data[i].position) + 0.5f) * (float) chunk::length, viewer);
                if(distance > furthest_distance) {
                    furthest_distance = distance;
                    furthest = (int) i;
                }
            }
            if(furthest >= 0) {
                bricks.release(brick_data[furthest].position);
                brick_data[furthest] = Brick();
                changed = true;
                id = bricks.allocate(chunk.pos);
            }
        }

        if(id >= 0) {
            while((int) pages.size() <= ProbeBricks::page(id)) createPage();
            if(brick_data.size() <= (size_t) id) brick_data.resize(id + 1);

            // The plants around the chunk : the air near them is traced
            std::vector<PlantBounds> plants;
            if(auto* library = reg.ctx().find<VegetationLibrary>()) {
                glm::vec3 low = chunk.origin(), high = low + glm::vec3((float) chunk::length);
                for(auto [entity, transform, plant] : reg.view<const Transform, const Plant>().each()) {
                    if(plant.species >= library->species.size()) continue;
                    const auto& species = library->species[plant.species];
                    float height = species.height * transform.scale.y, radius = species.radius * transform.scale.y;
                    if(height < 1.5f) continue;
                    float reach = radius + probes::reach;
                    const glm::vec3& at = transform.position;
                    if(at.x + reach < low.x || at.x - reach > high.x || at.z + reach < low.z || at.z - reach > high.z
                        || at.y + height + probes::reach < low.y || at.y - probes::spacing > high.y) continue;
                    plants.push_back({ at, radius, height });
                }
            }

            Brick& brick = brick_data[id];
            brick = Brick();
            brick.used = true;
            brick.chunk = nearest;
            brick.position = chunk.pos;
            brick.classes = classifyProbes(reg.get<ChunkVolume>(nearest).values, chunk.origin(), plants);
            brick.turn = schedule;

            glm::ivec3 offset = ProbeBricks::offset(id);
            ctx.transfer.updateImage(brick.classes.classes.data(), brick.classes.classes.size(), pages[ProbeBricks::page(id)]->classes->image,
                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(offset.x, offset.y, offset.z),
                vk::Extent3D(probes::size, probes::size, probes::size), vk::ImageLayout::eGeneral);

            changed = true;
            waiting--;
        }
    }

    if(changed) uploadIndex();

    auto& stats = reg.ctx().get<LightingStats>();
    stats.bricks = (uint32_t) bricks.used();
    // Chunks that could still get a brick count as probes to trace
    stats.probes_pending = bricks.used() < max_pages * ProbeBricks::page_bricks ? waiting : 0;

}

void ProbeVolume::update(float time) {

    Device& device = ctx.device;
    auto* settings = reg.ctx().find<RenderSettings>();
    auto& stats = reg.ctx().get<LightingStats>();

    glm::vec3 viewer(0.f);
    if(auto* camera = reg.ctx().find<Camera>()) viewer = camera->position;
    if(auto* input = reg.ctx().find<VRInput>(); input && input->session_running) {
        if(auto* rig = reg.ctx().find<VRRig>()) viewer = glm::vec3(rig->transform() * glm::vec4(input->head.position, 1.f));
    }

    syncBricks(viewer);

    frames++;

    // The view of the dispatches : only where the viewer is, for the map of the shade of the plants
    Ring& ring = rings[ctx.frame_index];
    {
        ViewUBO view {};
        view.view_projection = glm::mat4(1.f);
        view.position = glm::vec4(viewer, time);
        view.tree_shade = treeShadeView(reg);
        view.inverse_view_projection = glm::mat4(1.f);
        view.previous_view_projection = glm::mat4(1.f);
        view.previous_position = view.position;
        std::memcpy(ring.view_pointer, &view, sizeof(ViewUBO));
        ctx.writeViewSet(ring.view_set, ctx.frame_index, !ring.written);
        ring.written = true;
    }

    uint32_t rays = (uint32_t) std::clamp(settings ? settings->rays_per_probe : 64, 8, 256);
    uint32_t budget = (uint32_t) std::clamp(settings ? settings->probe_rays_per_frame : 250000, 0, 4000000) / rays;
    reserveFound(budget * (rays + sun_rays) + rays + sun_rays);

    stats.probes_active = 0;
    for(auto& brick : brick_data) {
        if(!brick.used) continue;
        stats.probes_active += (uint32_t) brick.classes.active.size();
        if(brick.sweeps == 0) stats.probes_pending += (uint32_t) (brick.classes.active.size() - brick.active_cursor) + (brick.fresh ? 1 : 0);
    }

    if(brick_data.empty()) return;

    vk::CommandBuffer command = ctx.compute.getCommandBuffer();
    command.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
    command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, layout, 0, { ring.view_set }, {});
    command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, layout, 2, { ring.work_set }, {});

    int bound_page = -1;
    uint32_t listed = 0;

    auto dispatch = [&](int id, uint32_t mode, uint32_t first, uint32_t count, float keep) {
        if(count == 0) return;
        const Brick& brick = brick_data[id];
        int page = ProbeBricks::page(id);
        if(page != bound_page) {
            command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, layout, 1, { pages[page]->set }, {});
            bound_page = page;
        }
        ProbePC pc {};
        pc.origin = glm::vec4(glm::vec3(brick.position * chunk::length), 0.f);
        pc.atlas = glm::ivec4(ProbeBricks::offset(id), 0);
        pc.mode = mode;
        pc.first = first;
        pc.count = count;
        pc.rays = rays;
        pc.keep = keep;
        pc.seed = (uint32_t) frames;
        command.pushConstants(layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(ProbePC), &pc);
        // A ray an invocation when tracing, a probe an invocation otherwise
        command.dispatch(((mode == Trace ? count * rays : count) + 63) / 64, 1, 1);
        // What comes next reads what this wrote
        command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {},
            vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite), {}, {});
    };

    // Copies a part of a list of a brick into the list of the frame, going round. Returns where it starts
    auto list = [&](const std::vector<uint32_t>& indices, size_t& cursor, uint32_t count, bool& wrapped) {
        uint32_t first = listed;
        wrapped = false;
        for(uint32_t i = 0; i < count; i++) {
            ring.work_pointer[listed++] = indices[cursor];
            if(++cursor >= indices.size()) {
                cursor = 0;
                wrapped = true;
            }
        }
        return first;
    };

    // Traces a share of the probes of a brick, then fills as large a share of its probes in the ground
    auto trace = [&](int id, uint32_t count) {
        Brick& brick = brick_data[id];
        count = std::min({ count, budget, (uint32_t) brick.classes.active.size(), work_capacity - listed });
        if(count == 0) return;
        float keep = brick.sweeps == 0 ? 0.f : keep_light;
        bool wrapped;
        uint32_t first = list(brick.classes.active, brick.active_cursor, count, wrapped);
        dispatch(id, Trace, first, count, keep);
        dispatch(id, Gather, first, count, keep);
        if(wrapped) brick.sweeps++;
        budget -= count;

        if(!brick.classes.fill.empty()) {
            uint32_t fill = (uint32_t) std::ceil((double) brick.classes.fill.size() * count / (double) brick.classes.active.size());
            fill = std::min({ fill, (uint32_t) brick.classes.fill.size(), work_capacity - listed });
            first = list(brick.classes.fill, brick.fill_cursor, fill, wrapped);
            dispatch(id, Fill, first, fill, 0.f);
        }
    };

    auto distanceTo = [&](const Brick& brick) {
        return glm::distance((glm::vec3(brick.position) + 0.5f) * (float) chunk::length, viewer);
    };

    // New bricks : today's look at once, everywhere
    for(size_t id = 0; id < brick_data.size(); id++) {
        Brick& brick = brick_data[id];
        if(!brick.used || !brick.fresh) continue;
        dispatch((int) id, First, 0, (uint32_t) probes::count, 0.f);
        brick.fresh = false;
        // Nothing to trace in it (under the ground, or all in the open air)
        if(brick.classes.active.empty()) brick.sweeps = 1;
    }

    // Probes within reach of a torch : every frame, and for a moment after the torch has moved on, with
    // less of what they held kept. Its light and its shadows follow it through the probes
    for(auto [entity, transform, light] : reg.view<const Transform, const PointLight>().each()) {
        if(light.intensity <= 0.f || light.radius <= 0.f) continue;
        glm::vec3 low = transform.position - light.radius, high = transform.position + light.radius;
        glm::ivec3 first_chunk = glm::ivec3(glm::floor(low / (float) chunk::length)), last_chunk = glm::ivec3(glm::floor(high / (float) chunk::length));
        for(int cz = first_chunk.z; cz <= last_chunk.z; cz++) {
            for(int cy = first_chunk.y; cy <= last_chunk.y; cy++) {
                for(int cx = first_chunk.x; cx <= last_chunk.x; cx++) {
                    int id = bricks.find(glm::ivec3(cx, cy, cz));
                    if(id < 0 || !brick_data[id].used) continue;
                    Brick& brick = brick_data[id];
                    glm::vec3 origin = glm::vec3(brick.position * chunk::length);
                    glm::ivec3 from = glm::max(glm::ivec3(glm::ceil((low - origin) / probes::spacing)), glm::ivec3(0));
                    glm::ivec3 to = glm::min(glm::ivec3(glm::floor((high - origin) / probes::spacing)), glm::ivec3(probes::size - 1));
                    for(int z = from.z; z <= to.z; z++) {
                        for(int y = from.y; y <= to.y; y++) {
                            for(int x = from.x; x <= to.x; x++) {
                                uint32_t index = probes::index(x, y, z);
                                if(brick.classes.classes[index] != probes::Active) continue;
                                if(glm::distance(origin + glm::vec3(x, y, z) * probes::spacing, transform.position) > light.radius) continue;
                                brick.hot[index] = hot_frames;
                            }
                        }
                    }
                }
            }
        }
    }
    for(size_t id = 0; id < brick_data.size(); id++) {
        Brick& brick = brick_data[id];
        if(!brick.used || brick.hot.empty()) continue;
        uint32_t first = listed, count = 0;
        for(auto it = brick.hot.begin(); it != brick.hot.end();) {
            if(count < budget && listed < work_capacity) {
                ring.work_pointer[listed++] = it->first;
                count++;
            }
            if(--it->second == 0) it = brick.hot.erase(it);
            else ++it;
        }
        dispatch((int) id, Trace, first, count, keep_hot);
        dispatch((int) id, Gather, first, count, keep_hot);
        budget -= count;
    }

    // Bricks that were never traced through, the nearest first : all the rays they can take
    {
        std::vector<std::pair<float, int>> order;
        for(size_t id = 0; id < brick_data.size(); id++) {
            if(brick_data[id].used && brick_data[id].sweeps == 0) order.emplace_back(distanceTo(brick_data[id]), (int) id);
        }
        std::sort(order.begin(), order.end());
        for(auto& [distance, id] : order) {
            if(budget == 0) break;
            trace(id, (uint32_t) (brick_data[id].classes.active.size() - brick_data[id].active_cursor));
        }
    }

    // The others in turn : each has its next turn at a time of the schedule, which comes sooner for the
    // bricks near the viewer. The earliest turns of this frame get a quarter of their probes traced
    {
        std::vector<uint8_t> served(brick_data.size(), 0);
        while(budget > 0) {
            int next = -1;
            for(size_t id = 0; id < brick_data.size(); id++) {
                const Brick& brick = brick_data[id];
                if(!brick.used || brick.sweeps == 0 || served[id] || brick.classes.active.empty()) continue;
                if(next < 0 || brick.turn < brick_data[next].turn) next = (int) id;
            }
            if(next < 0) break;
            Brick& brick = brick_data[next];
            served[next] = 1;
            schedule = std::max(schedule, brick.turn);
            uint32_t share = std::max<uint32_t>((uint32_t) brick.classes.active.size() / 4, 256);
            trace(next, share);
            float far = distanceTo(brick) / (float) chunk::length;
            double weight = 1. / (1. + (double) far * far);
            brick.turn = schedule + 0.25 / weight;
        }
    }

    // The open air of one brick a frame : the sky as it is now
    for(size_t tries = 0; tries < brick_data.size(); tries++) {
        open_cursor = (open_cursor + 1) % brick_data.size();
        if(!brick_data[open_cursor].used) continue;
        dispatch((int) open_cursor, Open, 0, (uint32_t) probes::count, 0.f);
        break;
    }

    vmaFlushAllocation(device, ring.work_buffer.allocation, 0, VK_WHOLE_SIZE);

    // The shaders of this frame read the probes
    command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eAllCommands, {},
        vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eMemoryRead), {}, {});

}

vk::DescriptorSet ProbeVolume::viewSet() const {
    return rings[ctx.frame_index].view_set;
}

ProbeVolume::~ProbeVolume() {

    Device& device = ctx.device;
    device->waitIdle();

    ctx.probe_index_view = nullptr;
    ctx.probe_light = nullptr;
    ctx.probe_direct = nullptr;

    for(auto& page : pages) {
        for(auto view : page->light_views) device->destroy(view);
        device->destroy(page->direct_view);
        device->destroy(page->classes_view);
    }
    device->destroy(pipeline);
    device->destroy(layout);
    device->destroy(pool);
    device->destroy(work_layout);
    device->destroy(page_layout);
    device->destroy(light_sampler);
    device->destroy(index_sampler);
    device->destroy(index_view);

}
