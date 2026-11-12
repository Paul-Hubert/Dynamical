#include "terrain_sys.h"

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"
#include "renderer/util/pipeline.h"

#include "logic/terrain/chunk.h"
#include "logic/components/camera.h"
#include "logic/components/render_settings.h"

#include "util/util.h"
#include "util/log.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cstring>

using namespace dy;

static const glm::uvec3 local_size(8, 4, 8);

struct MCPushConstants {
    glm::ivec3 pos;
    int32_t size;
    uint32_t max_vertices;
    uint32_t mode;
};

static vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

TerrainSys::TerrainSys(entt::registry& reg) : System(reg) {

}

void TerrainSys::init() {

    Context& ctx = *reg.ctx().get<Context*>();
    Device& device = ctx.device;

    // Created now so the render passes can read them from worker threads without creating them
    reg.storage<ChunkGPU>();
    reg.storage<ChunkReady>();
    reg.storage<ChunkTriangles>();

    ray_tracing = device.ray.enabled();

    vk::DeviceSize alignment = std::max<vk::DeviceSize>(device.physical.getProperties().limits.minStorageBufferOffsetAlignment, 16);

    // Indirect draw commands, one per slot, reset by the transfer before each march
    indirect_stride = alignUp(sizeof(vk::DrawIndirectCommand), alignment);
    {
        VmaAllocationCreateInfo info {};
        info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        indirect = VmaBuffer(device, &info, vk::BufferCreateInfo({}, max_slots * indirect_stride,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc,
            vk::SharingMode::eExclusive, 1, &device.g_i));
    }

    if(ray_tracing) {
        VmaAllocationCreateInfo info {};
        info.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
        info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        readback = VmaBuffer(device, &info, vk::BufferCreateInfo({}, max_slots * sizeof(vk::DrawIndirectCommand),
            vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive, 1, &device.g_i));
        VmaAllocationInfo inf;
        vmaGetAllocationInfo(device, readback.allocation, &inf);
        readback_pointer = static_cast<const vk::DrawIndirectCommand*>(inf.pMappedData);
    }

    // Density staging : a ring of max_per_frame chunks per compute command buffer
    staging_stride = alignUp(chunk::num_values * sizeof(float), alignment);
    {
        VmaAllocationCreateInfo info {};
        info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        staging = VmaBuffer(device, &info, vk::BufferCreateInfo({}, ctx.compute.ringSize() * max_per_frame * staging_stride,
            vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive, 1, &device.g_i));
        VmaAllocationInfo inf;
        vmaGetAllocationInfo(device, staging.allocation, &inf);
        staging_pointer = static_cast<uint8_t*>(inf.pMappedData);
    }

    // Marching cubes edge table
    {
        auto lookupData = dy::readFile("./resources/mclookup.bin");
        if(lookupData.size() != 256 * 16) {
            throw std::runtime_error("resources/mclookup.bin is missing or corrupted");
        }

        VmaAllocationCreateInfo info {};
        info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        lookupBuffer = VmaBuffer(device, &info, vk::BufferCreateInfo({}, lookupData.size(), vk::BufferUsageFlagBits::eUniformTexelBuffer,
            vk::SharingMode::eExclusive, 1, &device.g_i));

        VmaAllocationInfo inf;
        vmaGetAllocationInfo(device, lookupBuffer.allocation, &inf);
        std::memcpy(inf.pMappedData, lookupData.data(), lookupData.size());

        lookupView = device->createBufferView(vk::BufferViewCreateInfo({}, lookupBuffer, vk::Format::eR8Sint, 0, lookupData.size()));
    }

    // Descriptors

    {
        auto poolSizes = std::vector {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformTexelBuffer, 1),
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 2 * max_slots),
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, max_slots),
        };
        descPool = device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1 + max_slots, poolSizes));

        auto lookupBindings = std::vector {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlagBits::eCompute),
        };
        lookupLayout = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, lookupBindings));

        auto chunkBindings = std::vector {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
            vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBufferDynamic, 1, vk::ShaderStageFlagBits::eCompute),
        };
        chunkLayout = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, chunkBindings));

        lookupSet = device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &lookupLayout))[0];
        device->updateDescriptorSets({
            vk::WriteDescriptorSet(lookupSet, 0, 0, 1, vk::DescriptorType::eUniformTexelBuffer, nullptr, nullptr, &lookupView),
        }, {});
    }

    // Pipeline

    {
        auto pcrange = vk::PushConstantRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(MCPushConstants));
        auto layouts = std::vector { lookupLayout, chunkLayout };
        pipelineLayout = device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, pcrange));

        struct Constants {
            uint32_t x, y, z, values;
        } constants { local_size.x, local_size.y, local_size.z, (uint32_t) chunk::values };

        auto entries = std::vector {
            vk::SpecializationMapEntry(0, offsetof(Constants, x), sizeof(uint32_t)),
            vk::SpecializationMapEntry(1, offsetof(Constants, y), sizeof(uint32_t)),
            vk::SpecializationMapEntry(2, offsetof(Constants, z), sizeof(uint32_t)),
            vk::SpecializationMapEntry(3, offsetof(Constants, values), sizeof(uint32_t)),
        };
        auto specialization = vk::SpecializationInfo((uint32_t) entries.size(), entries.data(), sizeof(Constants), &constants);

        vk::ShaderModule shader = loadShader(device, "./resources/shaders/marchingcubes.comp.glsl.spv");
        pipeline = device->createComputePipeline(nullptr, vk::ComputePipelineCreateInfo({},
            vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, shader, "main", &specialization),
            pipelineLayout)).value;
        device->destroy(shader);
    }

    auto& buffers = reg.ctx().emplace<TerrainBuffers>();
    buffers.indirect = indirect;
    buffers.indirect_stride = indirect_stride;

}

uint32_t TerrainSys::allocateSlot() {

    if(!free_slots.empty()) {
        uint32_t slot = free_slots.back();
        free_slots.pop_back();
        return slot;
    }

    if(slots.size() >= max_slots) {
        return UINT32_MAX;
    }

    Context& ctx = *reg.ctx().get<Context*>();
    Device& device = ctx.device;

    uint32_t index = (uint32_t) slots.size();
    auto slot = std::make_unique<Slot>();

    VmaAllocationCreateInfo info {};
    info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    // The acceleration structure of the chunk is built from its vertices as they are (RayScene)
    vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer;
    if(ray_tracing) usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
    slot->vertices = VmaBuffer(device, &info, vk::BufferCreateInfo({}, (vk::DeviceSize) max_triangles * 3 * sizeof(TerrainVertex),
        usage, vk::SharingMode::eExclusive, 1, &device.g_i));

    slot->set = device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &chunkLayout))[0];

    auto vertexInfo = vk::DescriptorBufferInfo(slot->vertices, 0, VK_WHOLE_SIZE);
    auto indirectInfo = vk::DescriptorBufferInfo(indirect, index * indirect_stride, sizeof(vk::DrawIndirectCommand));
    auto valuesInfo = vk::DescriptorBufferInfo(staging, 0, chunk::num_values * sizeof(float));
    device->updateDescriptorSets({
        vk::WriteDescriptorSet(slot->set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &vertexInfo, nullptr),
        vk::WriteDescriptorSet(slot->set, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &indirectInfo, nullptr),
        vk::WriteDescriptorSet(slot->set, 2, 0, 1, vk::DescriptorType::eStorageBufferDynamic, nullptr, &valuesInfo, nullptr),
    }, {});

    reg.ctx().get<TerrainBuffers>().vertices.push_back(slot->vertices);
    slots.push_back(std::move(slot));

    return index;

}

void TerrainSys::tick(double dt) {

    Context& ctx = *reg.ctx().get<Context*>();

    frame_counter++;

    // Slots freed long enough ago are no longer read by any frame in flight
    for(auto it = pending_frees.begin(); it != pending_frees.end();) {
        if(frame_counter - it->frame > NUM_FRAMES + 1) {
            free_slots.push_back(it->slot);
            it = pending_frees.erase(it);
        } else {
            ++it;
        }
    }

    // The frame that counted these meshes is done (its fence was waited for) : how many triangles they have
    if(!pending_counts.empty()) {
        vmaInvalidateAllocation(ctx.device, readback.allocation, 0, VK_WHOLE_SIZE);
        for(auto it = pending_counts.begin(); it != pending_counts.end();) {
            if(frame_counter - it->frame < NUM_FRAMES) {
                ++it;
                continue;
            }
            auto* gpu = reg.valid(it->entity) ? reg.try_get<ChunkGPU>(it->entity) : nullptr;
            if(gpu && gpu->slot == it->slot) {
                reg.emplace_or_replace<ChunkTriangles>(it->entity, readback_pointer[it->slot].vertexCount / 3, it->slot);
            }
            it = pending_counts.erase(it);
        }
    }

    // Chunks out of view distance, once no worker writes to them anymore
    {
        auto view = reg.view<ChunkDestroying>(entt::exclude<ChunkGenerating>);
        std::vector<entt::entity> dead(view.begin(), view.end());
        for(auto entity : dead) {
            if(auto* gpu = reg.try_get<ChunkGPU>(entity)) {
                pending_frees.push_back({gpu->slot, frame_counter});
            }
            reg.destroy(entity);
        }
    }

    // Nearest pending chunks first

    auto* camera = reg.ctx().find<Camera>();
    glm::vec3 center = camera ? camera->position : glm::vec3(0.f);

    std::vector<std::pair<float, entt::entity>> todo;
    auto view = reg.view<ChunkC, ChunkVolume, ChunkMeshPending>(entt::exclude<ChunkDestroying>);
    for(auto entity : view) {
        todo.emplace_back(glm::distance2(view.get<ChunkC>(entity).center(), center), entity);
    }
    if(todo.empty()) {
        return;
    }

    // No more than the level of the graphics lets a frame mesh (RenderSettings::streaming)
    auto* render_settings = reg.ctx().find<RenderSettings>();
    const size_t most = render_settings ? (size_t) std::clamp(render_settings->streaming.chunk_meshes, 1, (int) max_per_frame) : max_per_frame;
    size_t count = std::min<size_t>(todo.size(), most);
    std::partial_sort(todo.begin(), todo.begin() + count, todo.end());
    todo.resize(count);

    DY_ZONE_N("Mesh chunks");
    DY_PLOT("Chunks meshed", (int64_t) count);

    vk::CommandBuffer command = ctx.compute.getCommandBuffer();
    DY_GPU_ZONE(ctx.gpu_profile, command, "Marching cubes");
    uint32_t ring = ctx.compute.ringIndex();

    struct Build {
        entt::entity entity;
        uint32_t slot;
        uint32_t staging_index;
    };
    std::vector<Build> builds;

    const vk::DrawIndirectCommand reset(0, 1, 0, 0);

    for(auto& [distance, entity] : todo) {
        uint32_t slot = allocateSlot();
        if(slot == UINT32_MAX) {
            dy::log(dy::warning) << "Terrain : out of chunk slots\n";
            break;
        }

        uint32_t staging_index = ring * max_per_frame + (uint32_t) builds.size();
        auto& values = reg.get<ChunkVolume>(entity).values;
        std::memcpy(staging_pointer + staging_index * staging_stride, values.data(), values.size() * sizeof(float));

        command.updateBuffer(indirect.buffer, slot * indirect_stride, sizeof(vk::DrawIndirectCommand), &reset);

        builds.push_back({entity, slot, staging_index});
    }

    if(builds.empty()) {
        return;
    }

    command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, {},
        vk::MemoryBarrier(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite), {}, {});

    command.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
    command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, {lookupSet}, {});

    const uint32_t max_vertices = max_triangles * 3;

    for(auto& build : builds) {
        auto& chunk = reg.get<ChunkC>(build.entity);
        uint32_t offset = (uint32_t) (build.staging_index * staging_stride);
        command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 1, {slots[build.slot]->set}, {offset});

        MCPushConstants pc { chunk.pos * chunk::length, chunk::cube_size, max_vertices, 0 };
        command.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(MCPushConstants), &pc);

        glm::uvec3 groups = glm::uvec3(chunk::cubes) / local_size;
        command.dispatch(groups.x, groups.y, groups.z);
    }

    command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {},
        vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite), {}, {});

    // Clamp the vertex counts of chunks that overflowed their buffer
    for(auto& build : builds) {
        uint32_t offset = (uint32_t) (build.staging_index * staging_stride);
        command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 1, {slots[build.slot]->set}, {offset});
        MCPushConstants pc { glm::ivec3(0), 0, max_vertices, 1 };
        command.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(MCPushConstants), &pc);
        command.dispatch(1, 1, 1);
    }

    if(ray_tracing) {
        command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer, {},
            vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead), {}, {});
        for(auto& build : builds) {
            command.copyBuffer(indirect.buffer, readback.buffer, vk::BufferCopy(build.slot * indirect_stride,
                build.slot * sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand)));
            pending_counts.push_back({ build.entity, build.slot, frame_counter });
        }
        // What comes next in this command buffer (the acceleration structures) reads the vertices
        command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eAllCommands, {},
            vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eMemoryRead), {}, {});
    }

    // Graphics waits on the compute submission of this frame, the meshes can be drawn right away
    for(auto& build : builds) {
        if(auto* gpu = reg.try_get<ChunkGPU>(build.entity)) {
            pending_frees.push_back({gpu->slot, frame_counter});
        }
        reg.emplace_or_replace<ChunkGPU>(build.entity, build.slot);
        reg.remove<ChunkTriangles>(build.entity);
        reg.emplace_or_replace<ChunkReady>(build.entity);
        reg.remove<ChunkMeshPending>(build.entity);
    }

}

TerrainSys::~TerrainSys() {

    Context& ctx = *reg.ctx().get<Context*>();
    Device& device = ctx.device;

    device->waitIdle();

    reg.clear<ChunkGPU>();
    reg.clear<ChunkReady>();
    reg.clear<ChunkTriangles>();
    reg.ctx().erase<TerrainBuffers>();

    slots.clear();

    device->destroy(pipeline);
    device->destroy(pipelineLayout);
    device->destroy(chunkLayout);
    device->destroy(lookupLayout);
    device->destroy(descPool);
    device->destroy(lookupView);

}
