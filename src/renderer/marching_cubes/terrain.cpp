#include "terrain.h"

#include "renderer/device.h"

#include "logic/components/chunk/chunkc.h"
#include "logic/components/positionc.h"
#include "chunk.h"
#include "logic/components/chunk/chunkdatac.h"
#include "marching_cubes.h"
#include "logic/components/renderinfo.h"

#include <iostream>

constexpr uint32_t NUM_TRIANGLES = 100000/3;
constexpr uint32_t NUM_INDIRECT = 10;
constexpr uint32_t MAX_CHUNKS = 5000;

Terrain::Terrain(entt::registry& reg) : System(reg) {
    
}

void Terrain::preinit() {
    
    //reg.on_construct<ChunkC>().connect<&Terrain::construction>(this);
    reg.on_destroy<Chunk>().connect<&Terrain::destructionChunk>(this);
    reg.on_destroy<ChunkBuild>().connect<&Terrain::destructionChunkBuild>(this);
    
}

void Terrain::init() {
    
    Device& device = *reg.ctx<Device*>();
    
    reg.set<StagedChunkData>();
    
    auto poolSizes = std::vector {
        vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, max_per_frame*NUM_FRAMES*2),
        vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, max_per_frame*NUM_FRAMES),
    };
    descPool = device->createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, max_per_frame*NUM_FRAMES, poolSizes.size(), poolSizes.data()));
    
    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
        vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBufferDynamic, 1, vk::ShaderStageFlagBits::eCompute),
    };
    descLayout = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings.size(), bindings.data()));
    
    triangles.reserve(MAX_CHUNKS);
    indirects.reserve(NUM_INDIRECT);
    
    indirects.push_back(IndirectAllocation(make_indirect(device, NUM_INDIRECT), NUM_INDIRECT));
    
    
    for(int i = 0; i < chunkData.size(); i++) {
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        
        chunkData[i] = VmaBuffer(device, &allocInfo, vk::BufferCreateInfo({}, max_per_frame * sizeof(ChunkData), vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive, 1, &device.c_i));
        
        StagedChunkData& cd = reg.ctx<StagedChunkData>();
        
        VmaAllocationInfo inf;
        vmaGetAllocationInfo(device, chunkData[i].allocation, &inf);
        cd.data[i] = static_cast<ChunkData*> (inf.pMappedData);
    }
    
    reg.set<TerrainDescriptorLayout>(descLayout);
    
}

void Terrain::construction(entt::registry& reg, entt::entity entity, ChunkC& chunk) {
    
    reg.assign<Chunk>(entity);
    
}

void Terrain::destructionChunk(entt::registry& reg, entt::entity entity) {
    
    deallocate(reg.get<Chunk>(entity));
    
}

void Terrain::destructionChunkBuild(entt::registry& reg, entt::entity entity) {
    
    deallocate(*reg.ctx<Device*>(), reg.get<ChunkBuild>(entity));
    
}


void Terrain::allocate(Chunk& chonk, ChunkBuild& build) {
    
    Device& device = *reg.ctx<Device*>();
    
    if(!indirectSlots.empty()) {
        const auto& slot = indirectSlots.top();
        chonk.indirect = indirects[slot.allocIndex].buffer;
        chonk.indirect_offset = slot.index;
        indirectSlots.pop();
    } else {
        auto& alloc = indirects.back();
        if(alloc.num < alloc.max) {
            chonk.indirect = alloc.buffer;
            chonk.indirect_offset = alloc.num;
            alloc.num++;
        } else {
            indirects.push_back(IndirectAllocation(make_indirect(device, NUM_INDIRECT), NUM_INDIRECT));
            auto& new_alloc = indirects.back();
            chonk.indirect = new_alloc.buffer;
            chonk.indirect_offset = new_alloc.num;
            new_alloc.num++;
        }
    }
    
    if(!triangleSlots.empty()) {
        const auto& slot = triangleSlots.top();
        chonk.triangles = triangles[slot];
        triangleSlots.pop();
    } else {
        //std::cout << (triangles.size() * NUM_TRIANGLES * sizeof(Triangle)) / 1000000. << std::endl;
        triangles.push_back(make_triangles(device, NUM_TRIANGLES));
        chonk.triangles = triangles.back();
    }
    chonk.triangles_offset = 0;
    
    build.set = device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &descLayout))[0];
    
    int ri = reg.ctx<StagedChunkData>().sync;
    auto triInfo = vk::DescriptorBufferInfo(chonk.triangles, 0, NUM_TRIANGLES * sizeof(Triangle));
    auto indInfo = vk::DescriptorBufferInfo(chonk.indirect, chonk.indirect_offset * sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
    auto uniformInfo = vk::DescriptorBufferInfo(chunkData[ri], 0, sizeof(ChunkData));
    
    device->updateDescriptorSets({
        vk::WriteDescriptorSet(build.set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &triInfo, nullptr),
        vk::WriteDescriptorSet(build.set, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &indInfo, nullptr),
        vk::WriteDescriptorSet(build.set, 2, 0, 1, vk::DescriptorType::eStorageBufferDynamic, nullptr, &uniformInfo, nullptr),
    }, {});
    
}

void Terrain::deallocate(Device& device, ChunkBuild& build) {
    
    device->freeDescriptorSets(descPool, build.set);
    
}

void Terrain::deallocate(Chunk& chonk) {
    
    for(uint32_t i = 0; i < indirects.size(); i++) {
        if(chonk.indirect == indirects[i].buffer) {
            indirectSlots.push(IndirectSlot{i, chonk.indirect_offset});
            break;
        }
    }
    
    for(uint32_t i = 0; i < triangles.size(); i++) {
        if(chonk.triangles == triangles[i].buffer) {
            triangleSlots.push(i);
            break;
        }
    }
    
}



void Terrain::tick() {
    
    Device& device = *reg.ctx<Device*>();
    
    auto view = reg.view<ChunkBuild, entt::tag<"modified"_hs>>();
    for(auto entity : view) {
        auto& build = view.get<ChunkBuild>(entity);
        
        Chunk::mutex.lock();
        if(reg.has<Chunk>(entity)) {
            auto& chonk = reg.get<Chunk>(entity);
            deallocate(chonk);
            deallocate(device, build);
            allocate(chonk, build);
        } else {
            auto& chonk = reg.assign<Chunk>(entity);
            allocate(chonk, build);
        }
        Chunk::mutex.unlock();
        
    }
    
}

void Terrain::finish() {
    
    Device& device = *reg.ctx<Device*>();
    
    device->waitIdle();
    
    device->destroy(descLayout);
    
    device->destroy(descPool);
    
}


Terrain::~Terrain() {
    
}

VmaBuffer Terrain::make_triangles(Device& device, uint32_t numTriangles) {
    
    VmaAllocationCreateInfo info = {};
    info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    bool concurrent = (device.g_i != device.c_i);
    uint32_t qfs[2] = {device.g_i, device.c_i};
    
    return dy::make_buffer(device, &info, vk::BufferCreateInfo(
        {}, numTriangles * sizeof(Triangle),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer,
        concurrent ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive, concurrent ? 2 : 1, &qfs[0]));
}

VmaBuffer Terrain::make_indirect(Device& device, uint32_t numIndirect) {
    
    VmaAllocationCreateInfo info = {};
    info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    bool concurrent = (device.g_i != device.c_i);
    uint32_t qfs[2] = {device.g_i, device.c_i};
    
    return dy::make_buffer(device, &info, vk::BufferCreateInfo(
        {}, numIndirect * sizeof(vk::DrawIndirectCommand), 
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer,
        concurrent ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive, concurrent ? 2 : 1, &qfs[0]));
    
}

