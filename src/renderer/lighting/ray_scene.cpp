#include "ray_scene.h"

#include <algorithm>
#include <cstring>

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"
#include "renderer/terrain/terrain_sys.h"

#include "renderer/model/model.h"

#include "logic/components/camera.h"
#include "logic/components/render_settings.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/components/vr.h"
#include "logic/vegetation/vegetation.h"
#include "view/components/model_renderable.h"
#include "logic/terrain/chunk.h"

#include "util/log.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

using namespace dy;

namespace {

constexpr uint32_t max_chunk_builds = 8;            // a frame
constexpr vk::DeviceSize first_scratch = 32ull << 20;

vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

}

RayScene::RayScene(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg), instance_buffers(ctx.compute.ringSize()) {

    reg.storage<ChunkLit>();
    reg.storage<ChunkTriangles>();
    reg.on_destroy<ChunkTriangles>().connect<&RayScene::onChunkGone>(*this);

    reg.ctx().emplace<LightingStats>();
    reg.ctx().emplace<RayMeshes>();
    reg.storage<PlantInRays>();

    // Entry 0 of the geometry table : the chunks
    GeometryEntry terrain;
    terrain.kind = (uint32_t) RayMesh::Kind::Terrain;
    addGeometries({ terrain });

    createTop(4096);

}

uint32_t RayScene::addGeometries(const std::vector<GeometryEntry>& entries) {

    Device& device = ctx.device;

    if(table_count + entries.size() > table_capacity) {
        uint32_t capacity = std::max<uint32_t>(4096, table_capacity * 2);
        while(capacity < table_count + entries.size()) capacity *= 2;

        VmaAllocationCreateInfo alloc {};
        alloc.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        alloc.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaBuffer grown(device, &alloc, vk::BufferCreateInfo({}, (vk::DeviceSize) capacity * sizeof(GeometryEntry),
            vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive, 1, &device.g_i));
        VmaAllocationInfo info;
        vmaGetAllocationInfo(device, grown.allocation, &info);
        auto* pointer = static_cast<GeometryEntry*>(info.pMappedData);
        if(table_count > 0) std::memcpy(pointer, table_pointer, table_count * sizeof(GeometryEntry));

        // The frames in flight read the old one
        if(table) retired.push_back({ Accel {}, std::move(table), 0 });
        table = std::move(grown);
        table_pointer = pointer;
        table_capacity = capacity;
        ctx.ray_geometries = table.buffer;
    }

    uint32_t first = table_count;
    std::memcpy(table_pointer + first, entries.data(), entries.size() * sizeof(GeometryEntry));
    table_count += (uint32_t) entries.size();
    vmaFlushAllocation(device, table.allocation, 0, VK_WHOLE_SIZE);
    return first;

}

void RayScene::createTop(uint32_t capacity) {

    Device& device = ctx.device;

    vk::AccelerationStructureGeometryKHR geometry(vk::GeometryTypeKHR::eInstances,
        vk::AccelerationStructureGeometryDataKHR(vk::AccelerationStructureGeometryInstancesDataKHR(VK_FALSE, {})));
    vk::AccelerationStructureBuildGeometryInfoKHR info(vk::AccelerationStructureTypeKHR::eTopLevel,
        vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild, vk::BuildAccelerationStructureModeKHR::eBuild, nullptr, nullptr, 1, &geometry);
    auto sizes = device->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, info, capacity);

    VmaAllocationCreateInfo alloc {};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    top.buffer = VmaBuffer(device, &alloc, vk::BufferCreateInfo({}, sizes.accelerationStructureSize,
        vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::SharingMode::eExclusive, 1, &device.g_i));
    top.handle = device->createAccelerationStructureKHR(vk::AccelerationStructureCreateInfoKHR({}, top.buffer, 0, sizes.accelerationStructureSize,
        vk::AccelerationStructureTypeKHR::eTopLevel));
    top.address = device->getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR(top.handle));

    top_scratch = VmaBuffer(device, &alloc, vk::BufferCreateInfo({}, sizes.buildScratchSize + device.ray.scratch_alignment,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::SharingMode::eExclusive, 1, &device.g_i));
    top_scratch_address = alignUp(device->getBufferAddress(vk::BufferDeviceAddressInfo(top_scratch)), device.ray.scratch_alignment);

    top_capacity = capacity;
    ctx.tlas = top.handle;

}

bool RayScene::queueBuild(Accel& out, vk::AccelerationStructureTypeKHR type, vk::BuildAccelerationStructureFlagsKHR flags,
    std::vector<vk::AccelerationStructureGeometryKHR> geometries, std::vector<uint32_t> primitives) {

    Device& device = ctx.device;

    auto build = std::make_unique<Build>();
    build->geometries = std::move(geometries);
    build->info = vk::AccelerationStructureBuildGeometryInfoKHR(type, flags, vk::BuildAccelerationStructureModeKHR::eBuild, nullptr, nullptr,
        (uint32_t) build->geometries.size(), build->geometries.data());
    auto sizes = device->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build->info, primitives);

    // The builds of a frame share one scratch buffer, each its part
    vk::DeviceSize need = alignUp(sizes.buildScratchSize, device.ray.scratch_alignment);
    if(scratch_used + need > scratch_size) {
        if(scratch_used > 0) return false;
        // Larger than the whole buffer : a new one (the old one may still be used by the frames in flight)
        if(scratch) retired.push_back({ Accel {}, std::move(scratch), 0 });
        scratch_size = std::max(first_scratch, need * 2);
        VmaAllocationCreateInfo alloc {};
        alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        scratch = VmaBuffer(device, &alloc, vk::BufferCreateInfo({}, scratch_size + device.ray.scratch_alignment,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::SharingMode::eExclusive, 1, &device.g_i));
        scratch_address = alignUp(device->getBufferAddress(vk::BufferDeviceAddressInfo(scratch)), device.ray.scratch_alignment);
    }

    VmaAllocationCreateInfo alloc {};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    out.buffer = VmaBuffer(device, &alloc, vk::BufferCreateInfo({}, sizes.accelerationStructureSize,
        vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::SharingMode::eExclusive, 1, &device.g_i));
    out.handle = device->createAccelerationStructureKHR(vk::AccelerationStructureCreateInfoKHR({}, out.buffer, 0, sizes.accelerationStructureSize, type));
    out.address = device->getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR(out.handle));

    build->info.dstAccelerationStructure = out.handle;
    build->info.scratchData.deviceAddress = scratch_address + scratch_used;
    scratch_used += need;

    for(uint32_t count : primitives) build->ranges.push_back(vk::AccelerationStructureBuildRangeInfoKHR(count, 0, 0, 0));

    reg.ctx().get<LightingStats>().structure_bytes += sizes.accelerationStructureSize;
    builds.push_back(std::move(build));
    return true;

}

void RayScene::flushBuilds(vk::CommandBuffer command) {

    if(builds.empty()) return;

    std::vector<vk::AccelerationStructureBuildGeometryInfoKHR> infos;
    std::vector<const vk::AccelerationStructureBuildRangeInfoKHR*> ranges;
    for(auto& build : builds) {
        infos.push_back(build->info);
        ranges.push_back(build->ranges.data());
    }
    command.buildAccelerationStructuresKHR(infos, ranges);

    // What is built next (the top level) reads these
    command.pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, {},
        vk::MemoryBarrier(vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eAccelerationStructureReadKHR | vk::AccessFlagBits::eAccelerationStructureWriteKHR), {}, {});

    builds.clear();
    scratch_used = 0;

}

void RayScene::retire(Accel&& accel) {
    if(accel.handle) retired.push_back({ std::move(accel), VmaBuffer(), 0 });
}

void RayScene::onChunkGone(entt::registry& registry, entt::entity entity) {
    auto it = chunks.find(entity);
    if(it == chunks.end()) return;
    retire(std::move(it->second.accel));
    free_geometries.push_back({ it->second.geometry, 0 });
    chunks.erase(it);
}

// An entry of the table for a chunk : one a chunk long gone had, or a new one
uint32_t RayScene::chunkGeometry(const GeometryEntry& entry) {
    for(auto it = free_geometries.begin(); it != free_geometries.end(); ++it) {
        if(it->frames <= (int) NUM_FRAMES + 1) continue;
        uint32_t index = it->index;
        free_geometries.erase(it);
        table_pointer[index] = entry;
        vmaFlushAllocation(ctx.device, table.allocation, 0, VK_WHOLE_SIZE);
        return index;
    }
    return addGeometries({ entry });
}

void RayScene::updateChunks() {

    auto* buffers = reg.ctx().find<TerrainBuffers>();
    auto& stats = reg.ctx().get<LightingStats>();
    if(!buffers) {
        stats.chunks_pending = 0;
        return;
    }

    // A chunk that was meshed again lost its structure with its triangle count
    {
        std::vector<entt::entity> stale;
        for(auto entity : reg.view<ChunkLit>()) {
            if(!reg.all_of<ChunkTriangles>(entity)) stale.push_back(entity);
        }
        reg.remove<ChunkLit>(stale.begin(), stale.end());
    }

    auto* camera = reg.ctx().find<Camera>();
    glm::vec3 centre = camera ? camera->position : glm::vec3(0.f);

    std::vector<std::pair<float, entt::entity>> todo;
    auto view = reg.view<ChunkC, ChunkGPU, ChunkTriangles>(entt::exclude<ChunkLit, ChunkDestroying>);
    for(auto entity : view) {
        if(view.get<ChunkTriangles>(entity).slot != view.get<ChunkGPU>(entity).slot) continue;
        todo.emplace_back(glm::distance2(view.get<ChunkC>(entity).center(), centre), entity);
    }
    std::sort(todo.begin(), todo.end());

    uint32_t built = 0;
    for(auto& [distance, entity] : todo) {
        if(built >= max_chunk_builds) break;
        const auto& triangles = reg.get<ChunkTriangles>(entity);
        if(triangles.count == 0) {
            reg.emplace<ChunkLit>(entity);
            continue;
        }

        // The vertex buffer of the slot as the marching cubes shader wrote it : a list of triangles,
        // positions first
        vk::AccelerationStructureGeometryTrianglesDataKHR data(vk::Format::eR32G32B32Sfloat,
            ctx.device->getBufferAddress(vk::BufferDeviceAddressInfo(buffers->vertices[triangles.slot])), sizeof(TerrainVertex),
            triangles.count * 3 - 1, vk::IndexType::eNoneKHR, {}, {});
        vk::AccelerationStructureGeometryKHR geometry(vk::GeometryTypeKHR::eTriangles, data, vk::GeometryFlagBitsKHR::eOpaque);

        ChunkAccel chunk;
        chunk.slot = triangles.slot;
        if(!queueBuild(chunk.accel, vk::AccelerationStructureTypeKHR::eBottomLevel, vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
            { geometry }, { triangles.count })) break;

        // What a ray that hits it reads : its triangles
        GeometryEntry entry;
        entry.vertices = data.vertexData.deviceAddress;
        entry.kind = (uint32_t) RayMesh::Kind::Terrain;
        entry.stride = sizeof(TerrainVertex) / sizeof(float);
        chunk.geometry = chunkGeometry(entry);
        chunks.emplace(entity, std::move(chunk));
        reg.emplace<ChunkLit>(entity);
        built++;
    }

    stats.chunks_lit = (uint32_t) chunks.size();
    uint32_t pending = 0;
    for(auto entity : reg.view<ChunkReady>(entt::exclude<ChunkLit, ChunkDestroying>)) pending++;
    stats.chunks_pending = pending;

}

// A structure for each mesh the passes gave since the last frame, and its entries in the geometry table
void RayScene::updateMeshes() {

    Device& device = ctx.device;
    auto& given = reg.ctx().get<RayMeshes>();
    auto& stats = reg.ctx().get<LightingStats>();

    // The textures of the leaves, for the sets of the frames to come
    ctx.ray_textures = &given.textures;

    meshes.resize(given.meshes.size());

    uint32_t pending = 0;
    bool full = false;
    for(size_t m = 0; m < meshes.size(); m++) {
        MeshAccel& mesh = meshes[m];
        if(mesh.built) continue;
        if(full) {
            pending++;
            continue;
        }

        const RayMesh& source = given.meshes[m];
        vk::DeviceAddress vertices = device->getBufferAddress(vk::BufferDeviceAddressInfo(source.vertices->buffer))
            + (vk::DeviceAddress) source.first_vertex * source.vertex_stride;
        vk::DeviceAddress indices = device->getBufferAddress(vk::BufferDeviceAddressInfo(source.indices->buffer));

        std::vector<vk::AccelerationStructureGeometryKHR> geometries;
        std::vector<uint32_t> primitives;
        std::vector<GeometryEntry> entries;
        for(auto& part : source.parts) {
            vk::DeviceAddress first = indices + (vk::DeviceAddress) part.first_index * sizeof(uint32_t);
            vk::AccelerationStructureGeometryTrianglesDataKHR data(vk::Format::eR32G32B32Sfloat, vertices, source.vertex_stride,
                source.vertex_count - 1, vk::IndexType::eUint32, first, {});
            // The leaves are tested against their texture by the ray, once each
            geometries.push_back(vk::AccelerationStructureGeometryKHR(vk::GeometryTypeKHR::eTriangles, data,
                part.opaque ? vk::GeometryFlagBitsKHR::eOpaque : vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation));
            primitives.push_back(part.index_count / 3);

            GeometryEntry entry;
            entry.vertices = vertices;
            entry.indices = first;
            entry.texture = part.opaque ? -1 : part.texture;
            entry.alpha_cutoff = part.alpha_cutoff;
            entry.color = part.color;
            entry.kind = (uint32_t) source.kind;
            entry.transmission = part.transmission;
            entry.stride = source.vertex_stride / sizeof(float);
            entries.push_back(entry);
        }

        if(geometries.empty() || !queueBuild(mesh.accel, vk::AccelerationStructureTypeKHR::eBottomLevel,
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace, geometries, primitives)) {
            full = !geometries.empty();
            if(full) pending++;
            else mesh.built = true;     // nothing to trace
            continue;
        }
        mesh.first_geometry = addGeometries(entries);
        mesh.built = true;
    }
    stats.structures_pending = pending;

}

void RayScene::addInstance(const glm::mat4& matrix, vk::DeviceAddress structure, uint32_t first_geometry, uint32_t mask) {
    vk::AccelerationStructureInstanceKHR instance;
    // Row major, three rows
    for(int row = 0; row < 3; row++) {
        for(int column = 0; column < 4; column++) instance.transform.matrix[row][column] = matrix[column][row];
    }
    instance.instanceCustomIndex = first_geometry;
    instance.mask = mask;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = (VkGeometryInstanceFlagsKHR) vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable;
    instance.accelerationStructureReference = structure;
    instances.push_back(instance);
}

// What the top level structure of this frame holds
void RayScene::gatherInstances() {

    auto& stats = reg.ctx().get<LightingStats>();
    auto* settings = reg.ctx().find<RenderSettings>();

    instances.clear();

    for(auto& [entity, chunk] : chunks) addInstance(glm::mat4(1.f), chunk.accel.address, chunk.geometry, Terrain);

    // Who looks, and how far plants are their leaves for them : one scene for both targets, the larger range
    std::vector<glm::vec3> viewers;
    float range = 0.f;
    if(auto* camera = reg.ctx().find<Camera>()) {
        viewers.push_back(camera->position);
        if(settings) range = settings->lighting[0].plant_geometry_range;
    }
    if(auto* input = reg.ctx().find<VRInput>(); input && input->session_running) {
        if(auto* rig = reg.ctx().find<VRRig>()) {
            viewers.push_back(glm::vec3(rig->transform() * glm::vec4(input->head.position, 1.f)));
            if(settings) range = std::max(range, settings->lighting[1].plant_geometry_range);
        }
    }

    // Plants : within the range of a viewer, and until a little further once in (no flicker on the edge)
    stats.plants_near = 0;
    stats.plants_far = 0;
    if(auto* plant_meshes = reg.ctx().find<RayPlantMeshes>()) {
        const float enter2 = range * range, leave2 = enter2 * 1.15f * 1.15f;
        std::vector<entt::entity> entering, leaving;
        auto view = reg.view<const Transform, const Plant>();
        for(auto [entity, transform, plant] : view.each()) {
            float distance2 = 1e30f;
            for(auto& viewer : viewers) distance2 = std::min(distance2, glm::distance2(viewer, transform.position));
            bool was = reg.all_of<PlantInRays>(entity);
            bool in = distance2 < (was ? leave2 : enter2);
            if(in != was) (in ? entering : leaving).push_back(entity);
            if(!in) {
                stats.plants_far++;
                continue;
            }
            if(plant.species >= plant_meshes->mesh.size()) continue;
            int32_t id = plant_meshes->mesh[plant.species];
            if(id < 0 || (size_t) id >= meshes.size() || !meshes[id].built || !meshes[id].accel.handle) continue;
            addInstance(transform.matrix(), meshes[id].accel.address, meshes[id].first_geometry, Plants);
            stats.plants_near++;
        }
        for(auto entity : entering) reg.emplace<PlantInRays>(entity);
        for(auto entity : leaving) reg.remove<PlantInRays>(entity);
    }

    // Models : boxes, hands, what the scenes place. Those physics or a hand move are left out of the rays
    // of the probes
    for(auto [entity, transform, renderable] : reg.view<const Transform, const ModelRenderable>().each()) {
        const ModelC* model = reg.try_get<ModelC>(renderable.model);
        if(!model || model->ray_mesh < 0 || (size_t) model->ray_mesh >= meshes.size()) continue;
        const MeshAccel& mesh = meshes[model->ray_mesh];
        if(!mesh.built || !mesh.accel.handle) continue;
        auto* body = reg.try_get<RigidBody>(entity);
        bool moves = body && body->motion != RigidBody::Motion::Static;
        addInstance(transform.matrix(), mesh.accel.address, mesh.first_geometry, moves ? Dynamic : Static);
    }

}

void RayScene::buildTop(vk::CommandBuffer command) {

    Device& device = ctx.device;

    // The buffer of this compute command buffer : the GPU is done with what it held
    auto& buffer = instance_buffers[ctx.compute.ringIndex()];
    if(!buffer.buffer || buffer.capacity < instances.size()) {
        buffer.capacity = std::max<uint32_t>(4096, (uint32_t) instances.size() * 2);
        VmaAllocationCreateInfo alloc {};
        alloc.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        alloc.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        buffer.buffer = VmaBuffer(device, &alloc, vk::BufferCreateInfo({}, (vk::DeviceSize) buffer.capacity * sizeof(vk::AccelerationStructureInstanceKHR),
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            vk::SharingMode::eExclusive, 1, &device.g_i));
        VmaAllocationInfo info;
        vmaGetAllocationInfo(device, buffer.buffer.allocation, &info);
        buffer.pointer = static_cast<vk::AccelerationStructureInstanceKHR*>(info.pMappedData);
        buffer.address = device->getBufferAddress(vk::BufferDeviceAddressInfo(buffer.buffer));
    }
    if(!instances.empty()) {
        std::memcpy(buffer.pointer, instances.data(), instances.size() * sizeof(vk::AccelerationStructureInstanceKHR));
        vmaFlushAllocation(device, buffer.buffer.allocation, 0, VK_WHOLE_SIZE);
    }

    if(instances.size() > top_capacity) {
        retire(std::move(top));
        retired.push_back({ Accel {}, std::move(top_scratch), 0 });
        uint32_t capacity = top_capacity;
        while(capacity < instances.size()) capacity *= 2;
        createTop(capacity);
    }

    vk::AccelerationStructureGeometryInstancesDataKHR data(VK_FALSE, buffer.address);
    vk::AccelerationStructureGeometryKHR geometry(vk::GeometryTypeKHR::eInstances, vk::AccelerationStructureGeometryDataKHR(data));
    vk::AccelerationStructureBuildGeometryInfoKHR info(vk::AccelerationStructureTypeKHR::eTopLevel,
        vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild, vk::BuildAccelerationStructureModeKHR::eBuild, nullptr, top.handle, 1, &geometry);
    info.scratchData.deviceAddress = top_scratch_address;
    vk::AccelerationStructureBuildRangeInfoKHR range((uint32_t) instances.size(), 0, 0, 0);
    const vk::AccelerationStructureBuildRangeInfoKHR* ranges = &range;
    command.buildAccelerationStructuresKHR(info, ranges);

    // The shaders of this frame trace it
    command.pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::PipelineStageFlagBits::eAllCommands, {},
        vk::MemoryBarrier(vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eMemoryRead), {}, {});

    reg.ctx().get<LightingStats>().instances = (uint32_t) instances.size();

}

void RayScene::update() {

    // Structures let go once no frame in flight traces them
    for(auto& entry : free_geometries) entry.frames++;
    for(auto it = retired.begin(); it != retired.end();) {
        if(++it->frames > (int) NUM_FRAMES + 1) {
            if(it->accel.handle) ctx.device->destroyAccelerationStructureKHR(it->accel.handle);
            it = retired.erase(it);
        } else {
            ++it;
        }
    }

    vk::CommandBuffer command = ctx.compute.getCommandBuffer();

    // The frames before may still trace the structures and use the scratch memory : after them
    command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {},
        vk::MemoryBarrier(vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite), {}, {});

    updateChunks();
    updateMeshes();
    flushBuilds(command);
    gatherInstances();
    buildTop(command);

}

RayScene::~RayScene() {

    reg.on_destroy<ChunkTriangles>().disconnect<&RayScene::onChunkGone>(*this);
    ctx.device->waitIdle();

    for(auto& [entity, chunk] : chunks) ctx.device->destroyAccelerationStructureKHR(chunk.accel.handle);
    for(auto& mesh : meshes) {
        if(mesh.accel.handle) ctx.device->destroyAccelerationStructureKHR(mesh.accel.handle);
    }
    ctx.ray_geometries = nullptr;
    ctx.ray_textures = nullptr;
    reg.clear<PlantInRays>();
    reg.ctx().erase<RayMeshes>();
    for(auto& old : retired) {
        if(old.accel.handle) ctx.device->destroyAccelerationStructureKHR(old.accel.handle);
    }
    if(top.handle) ctx.device->destroyAccelerationStructureKHR(top.handle);
    ctx.tlas = nullptr;
    reg.clear<ChunkLit>();

}
