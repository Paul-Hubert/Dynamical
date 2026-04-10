#include "map_upload.h"

#include "renderer/context/context.h"
#include "renderer/util/vk_debug.h"

#include "imgui.h"
#include <string.h>

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"
#include <algorithm>

#include "util/util.h"
#include "util/log.h"
#include "util/color.h"

#include "logic/map/tile.h"
#include "logic/map/chunk.h"
#include "logic/map/map_manager.h"

#include "logic/components/camera.h"
#include "logic/components/renderable.h"
#include "logic/components/map_upload_data.h"
#include "logic/components/building.h"
#include "logic/components/position.h"

using namespace dy;

constexpr int max_objects = 20000;
constexpr int max_buildings = 2000;

MapUploadSys::MapUploadSys(entt::registry& reg) : System(reg) {

    Context& ctx = *reg.ctx<Context*>();

    MapUploadData& data = reg.set<MapUploadData>();

    per_frame.resize(NUM_FRAMES);

    stored_chunks.resize(max_stored_chunks);

    for(int i = 0; i< per_frame.size(); i++) {
        auto& f = per_frame[i];

        VmaAllocationCreateInfo info {};
        info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        f.stagingBuffer = VmaBuffer(ctx.device, &info, vk::BufferCreateInfo({}, sizeof(Header) + max_chunks * sizeof(RenderChunk), vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive));

        SET_VK_NAME_FMT(ctx.device, vk::ObjectType::eBuffer, f.stagingBuffer.buffer, "MapUpload_StagingBuffer_F%d", i);

        VmaAllocationInfo inf;
        vmaGetAllocationInfo(ctx.device, f.stagingBuffer.allocation, &inf);

        f.stagingPointer = reinterpret_cast<Header*> (inf.pMappedData);
    }

    {

        VmaAllocationCreateInfo info {};
        info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        storageBuffer = VmaBuffer(ctx.device, &info, vk::BufferCreateInfo({}, sizeof(Header) + max_stored_chunks * sizeof(RenderChunk), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive));

        SET_VK_NAME(ctx.device, vk::ObjectType::eBuffer, storageBuffer.buffer, "MapData_StorageBuffer");

    }

    {
        auto poolSizes = std::vector {
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1),
            };
        descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, (uint32_t) poolSizes.size(), poolSizes.data()));
    }

    {
        auto bindings = std::vector {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute)
        };
        data.mapLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, (uint32_t) bindings.size(), bindings.data()));

        data.mapSet = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &data.mapLayout))[0];

        auto info = vk::DescriptorBufferInfo(storageBuffer, 0, storageBuffer.size);
        auto write = vk::WriteDescriptorSet(data.mapSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &info, nullptr);
        ctx.device->updateDescriptorSets(1, &write, 0, nullptr);

    }

    for(int i = 0; i< per_frame.size(); i++) {
        auto& f = per_frame[i];

        VmaAllocationCreateInfo info {};
        info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        f.objectBuffer = VmaBuffer(ctx.device, &info, vk::BufferCreateInfo({}, sizeof(RenderObject) * max_objects, vk::BufferUsageFlagBits::eVertexBuffer, vk::SharingMode::eExclusive));

        SET_VK_NAME_FMT(ctx.device, vk::ObjectType::eBuffer, f.objectBuffer.buffer, "MapUpload_ObjectBuffer_F%d", i);

        VmaAllocationInfo inf;
        vmaGetAllocationInfo(ctx.device, f.objectBuffer.allocation, &inf);

        f.objectPointer = reinterpret_cast<RenderObject*> (inf.pMappedData);

        // Building buffer
        f.buildingBuffer = VmaBuffer(ctx.device, &info, vk::BufferCreateInfo({}, sizeof(RenderBuilding) * max_buildings, vk::BufferUsageFlagBits::eVertexBuffer, vk::SharingMode::eExclusive));

        SET_VK_NAME_FMT(ctx.device, vk::ObjectType::eBuffer, f.buildingBuffer.buffer, "MapUpload_BuildingBuffer_F%d", i);

        vmaGetAllocationInfo(ctx.device, f.buildingBuffer.allocation, &inf);

        f.buildingPointer = reinterpret_cast<RenderBuilding*> (inf.pMappedData);
    }
}

int MapUploadSys::computeLOD(glm::ivec2 chunk_pos, glm::ivec2 camera_chunk) const {
    int dist = std::max(std::abs(chunk_pos.x - camera_chunk.x), std::abs(chunk_pos.y - camera_chunk.y));
    if (dist <= lod0_distance) return 0;
    if (dist <= lod1_distance) return 1;
    return 2;
}

void MapUploadSys::tick(double dt) {

    OPTICK_EVENT();

    Context& ctx = *reg.ctx<Context*>();

    MapUploadData& data = reg.ctx<MapUploadData>();

    auto& f = per_frame[ctx.frame_index];

    auto transfer = ctx.transfer.getCommandBuffer();

    std::vector<vk::BufferCopy> regions;

    auto header = f.stagingPointer;

    memset(header, 0, sizeof(Header));

    header->colors[Tile::nothing] = glm::vec4(0,0,0,0);
    header->colors[Tile::dirt] = glm::vec4(0.6078,0.4627,0.3255,1);
    header->colors[Tile::grass] = glm::vec4(0.3373,0.4902,0.2745,1);
    header->colors[Tile::stone] = glm::vec4(0.6118,0.6353,0.7216,1);
    header->colors[Tile::sand] = glm::vec4(0.761,0.698,0.502,1);
    header->colors[Tile::river] = Color("74ccf4").rgba;
    header->colors[Tile::water] = Color("2389da").rgba;

    regions.push_back(vk::BufferCopy(0, 0, sizeof(Header)));

    int staging_counter = 0;

    auto objectBuffer = f.objectPointer;

    data.objectBuffer = f.objectBuffer.buffer;

    auto buildingBuffer = f.buildingPointer;

    data.buildingBuffer = f.buildingBuffer.buffer;

    auto& map = reg.ctx<MapManager>();
    auto& camera = reg.ctx<Camera>();

    // 1. Poll async chunk generation and process finalization queue
    map.pollReady();
    map.processFinalizationQueue(max_finalizations_per_frame);

    // 2. Calculate visible chunk bounds
    auto screen_size = camera.getScreenSize();
    screen_size.y *= 2;

    std::vector<glm::vec2> corners{
            glm::vec2(),
            glm::vec2(screen_size.x, 0),
            glm::vec2(0,screen_size.y),
            camera.getScreenSize()
    };

    int maxvalue = 10000000;
    glm::ivec2 max_pos(-maxvalue, -maxvalue);
    glm::ivec2 min_pos(maxvalue, maxvalue);

    for(auto corner : corners) {
        auto corner_rpos = camera.fromScreenSpace(corner);
        auto corner_pos = map.getChunkPos(corner_rpos);
        max_pos.x = std::max(max_pos.x, corner_pos.x);
        max_pos.y = std::max(max_pos.y, corner_pos.y);
        min_pos.x = std::min(min_pos.x, corner_pos.x);
        min_pos.y = std::min(min_pos.y, corner_pos.y);
    }

    auto corner_pos = min_pos;
    auto end_pos = max_pos;

    // Camera center for LOD and distance sorting
    glm::ivec2 camera_chunk = map.getChunkPos(camera.getCenter());

    // 3. Collect all visible chunk positions and sort by distance from camera
    struct ChunkEntry {
        glm::ivec2 pos;
        int lod;
        int dist;
    };
    std::vector<ChunkEntry> visible_chunks;
    visible_chunks.reserve((end_pos.x - corner_pos.x + 1) * (end_pos.y - corner_pos.y + 1));

    for (int x = corner_pos.x; x <= end_pos.x; x++) {
        for (int y = corner_pos.y; y <= end_pos.y; y++) {
            glm::ivec2 pos(x, y);
            int lod = computeLOD(pos, camera_chunk);
            int dist = std::max(std::abs(x - camera_chunk.x), std::abs(y - camera_chunk.y));
            visible_chunks.push_back({pos, lod, dist});
        }
    }

    std::sort(visible_chunks.begin(), visible_chunks.end(),
              [](const ChunkEntry& a, const ChunkEntry& b) { return a.dist < b.dist; });

    // 4. Process visible chunks with async generation budgets
    // Group by LOD: fill chunk_positions with LOD 0 first, then LOD 1, then LOD 2
    struct ProcessedChunk {
        glm::ivec2 pos;
        int lod;
        Chunk* chunk;
    };
    std::vector<ProcessedChunk> lod0_chunks, lod1_chunks, lod2_chunks;

    data.num_chunks = 0;
    data.num_objects = 0;
    data.num_buildings = 0;
    data.num_chunks_lod0 = 0;
    data.num_chunks_lod1 = 0;
    data.num_chunks_lod2 = 0;

    int async_requests_this_frame = 0;

    for (auto& entry : visible_chunks) {
        auto chunk_pos = entry.pos;
        int lod = entry.lod;

        Chunk* chunk = map.getChunk(chunk_pos);

        if (chunk == nullptr) {
            // Not generated yet — request async if budget allows
            if (!map.isChunkPending(chunk_pos) && async_requests_this_frame < max_async_requests_per_frame) {
                map.requestChunkAsync(chunk_pos, lod);
                async_requests_this_frame++;
            }
            continue; // invisible until ready
        }

        // Check if chunk needs LOD upgrade (was generated at lower resolution, now closer)
        if (chunk->lod_level > lod) {
            // Request re-generation at higher resolution
            if (!map.isChunkPending(chunk_pos) && async_requests_this_frame < max_async_requests_per_frame) {
                map.requestChunkAsync(chunk_pos, lod);
                async_requests_this_frame++;
            }
            // Continue rendering old LOD data in the meantime
        }

        // Categorize by LOD for grouped draw calls
        switch (lod) {
            case 0: lod0_chunks.push_back({chunk_pos, lod, chunk}); break;
            case 1: lod1_chunks.push_back({chunk_pos, lod, chunk}); break;
            default: lod2_chunks.push_back({chunk_pos, lod, chunk}); break;
        }
    }

    // 5. Fill chunk_positions and upload tile data, grouped by LOD
    // Instance indices: [LOD0 chunks][LOD1 chunks][LOD2 chunks]
    int instance_index = 0;

    auto processChunkGroup = [&](std::vector<ProcessedChunk>& group) -> int {
        int count = 0;
        for (auto& pc : group) {
            OPTICK_EVENT("MapUploadSys::tick::chunk");

            if (instance_index >= max_chunks) break;

            // Set chunk position for this instance
            header->chunk_positions[instance_index] = pc.pos;

            // Add Objects from chunk
            if (data.num_objects < max_objects) {
                for (auto entity : pc.chunk->getObjects()) {
                    if (reg.all_of<Renderable>(entity) && !reg.all_of<Building>(entity)) {
                        auto& position = reg.get<Position>(entity);
                        auto& renderable = reg.get<Renderable>(entity);
                        objectBuffer[data.num_objects].sphere.x = position.x;
                        objectBuffer[data.num_objects].sphere.y = position.y;
                        objectBuffer[data.num_objects].sphere.z = position.getHeight() + 0.5f;
                        objectBuffer[data.num_objects].sphere.w = 0.5f;
                        objectBuffer[data.num_objects].color = renderable.color.rgba;

                        data.num_objects++;

                        if (data.num_objects >= max_objects) break;
                    }
                }
            }

            // Upload tile data to GPU
            bool stored = false;
            int index = find_stored_chunk(pc.pos);
            if (index != -1) stored = true;

            if (!stored) {
                index = find_storage_slot();

                RenderChunk* rchunk = reinterpret_cast<RenderChunk*>(header + 1);

                for (int i = 0; i < Chunk::size; i++) {
                    for (int j = 0; j < Chunk::size; j++) {
                        Tile& tile = pc.chunk->get(glm::ivec2(i, j));
                        rchunk[staging_counter].tiles[i * Chunk::size + j] = {tile.terrain, tile.level};
                    }
                }

                regions.push_back(vk::BufferCopy(sizeof(Header) + staging_counter * sizeof(RenderChunk), sizeof(Header) + index * sizeof(RenderChunk), sizeof(RenderChunk)));

                staging_counter++;
                pc.chunk->setUpdated(false);
            }

            insert_chunk(header, pc.pos, index);

            StoredChunk& sc = stored_chunks[index];
            sc.position = pc.pos;
            sc.stored = true;
            sc.chunk = pc.chunk;
            sc.used = true;

            data.num_chunks++;
            instance_index++;
            count++;
        }
        return count;
    };

    data.num_chunks_lod0 = processChunkGroup(lod0_chunks);
    data.num_chunks_lod1 = processChunkGroup(lod1_chunks);
    data.num_chunks_lod2 = processChunkGroup(lod2_chunks);

    for(int i = 0; i<stored_chunks.size(); i++) {
        stored_chunks[i].used = false;
    }

    // Upload buildings
    for (auto [entity, building, position] : reg.view<Building, Position>().each()) {
        if (data.num_buildings < max_buildings) {
            auto& building_tmpl = get_building_templates()[building.type];

            float terrain_height = 0.0f;
            int height_count = 0;
            for (const auto& tile_pos : building.occupied_tiles) {
                if (auto* tile = map.getTile(glm::vec2(tile_pos))) {
                    terrain_height += tile->level;
                    height_count++;
                }
            }
            if (height_count > 0) {
                terrain_height /= height_count;
            }

            buildingBuffer[data.num_buildings].position = glm::vec4(position.x, position.y, terrain_height, 0.0f);

            float wall_height = building_tmpl.wall_height * building.construction_progress;
            float roof_height = building_tmpl.roof_height * building.construction_progress;
            buildingBuffer[data.num_buildings].dimensions = glm::vec4(
                static_cast<float>(building_tmpl.footprint.x),
                static_cast<float>(building_tmpl.footprint.y),
                wall_height,
                roof_height
            );

            // Wood color for walls, darker for roof
            buildingBuffer[data.num_buildings].wall_color = glm::vec4(0.55f, 0.35f, 0.17f, 1.0f);
            buildingBuffer[data.num_buildings].roof_color = glm::vec4(0.45f, 0.28f, 0.14f, 1.0f);

            data.num_buildings++;
        }
    }

    transfer.copyBuffer(f.stagingBuffer, storageBuffer, regions);

    ImGui::End();

}


MapUploadSys::~MapUploadSys() {

    Context& ctx = *reg.ctx<Context*>();

    ctx.device->destroy(descPool);

    MapUploadData& data = reg.ctx<MapUploadData>();

    ctx.device->destroy(data.mapLayout);

}

uint32_t MapUploadSys::hash(glm::ivec2 chunk_pos) {
    return chunk_pos.x * (1<<16) + chunk_pos.y + 1;
}

void MapUploadSys::insert_chunk(Header* header, glm::ivec2 chunk_pos, int index) {

    uint32_t key = hash(chunk_pos);

    uint32_t slot = key % max_chunks;
    while(true) {
        if(header->chunk_indices[slot].key == 0) {
            header->chunk_indices[slot].key = key;
            header->chunk_indices[slot].value = index;
            return;
        }
        slot = (slot+1) % max_chunks;
    }

}

int MapUploadSys::find_stored_chunk(glm::ivec2 chunk_pos) {

    for(int i = 0; i<stored_chunks.size(); i++) {
        StoredChunk& sc = stored_chunks[i];
        if(sc.stored && sc.position == chunk_pos) {
            if(sc.chunk->isUpdated()) {
                sc.stored = false;
                return -1;
            }
            return i;
        }
    }
    return -1;

}

int MapUploadSys::find_storage_slot() {

    StoredChunk& sc = stored_chunks[storage_counter];
    while(sc.stored
            && (sc.used
            )) {
        storage_counter = (storage_counter+1)%stored_chunks.size();
        sc = stored_chunks[storage_counter];
    }
    int index = storage_counter;
    storage_counter = (storage_counter+1)%stored_chunks.size();
    return index;

}
