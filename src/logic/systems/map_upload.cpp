#include "map_upload.h"

#include "renderer/context/context.h"

#include "imgui.h"
#include <string.h>

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

#include "util/util.h"
#include "util/log.h"
#include "util/color.h"

#include "logic/map/tile.h"
#include "logic/map/chunk.h"
#include "logic/map/map_manager.h"

#include "logic/components/camera.h"

using namespace dy;

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

        VmaAllocationInfo inf;
        vmaGetAllocationInfo(ctx.device, f.stagingBuffer.allocation, &inf);

        f.stagingPointer = inf.pMappedData;
    }

    {

        VmaAllocationCreateInfo info {};
        info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        storageBuffer = VmaBuffer(ctx.device, &info, vk::BufferCreateInfo({}, sizeof(Header) + max_stored_chunks * sizeof(RenderChunk), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive));

    }

    {
        auto poolSizes = std::vector {
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1),
            };
        descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, (uint32_t) poolSizes.size(), poolSizes.data()));
    }

    {
        auto bindings = std::vector {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        };
        data.descLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, (uint32_t) bindings.size(), bindings.data()));

        data.descSet = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &data.descLayout))[0];

        auto info = vk::DescriptorBufferInfo(storageBuffer, 0, storageBuffer.size);
        auto write = vk::WriteDescriptorSet(data.descSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &info, nullptr);
        ctx.device->updateDescriptorSets(1, &write, 0, nullptr);

    }

}

void MapUploadSys::tick(float dt) {

    OPTICK_EVENT();

    Context& ctx = *reg.ctx<Context*>();
    
    MapUploadData& data = reg.ctx<MapUploadData>();

    auto& f = per_frame[ctx.frame_index];

    auto transfer = ctx.transfer.getCommandBuffer();

    std::vector<vk::BufferCopy> regions;

    auto header = reinterpret_cast<Header*> (f.stagingPointer);

    memset(header, 0, sizeof(Header));

    header->colors[Tile::nothing] = glm::vec4(0,0,0,0);
    header->colors[Tile::dirt] = glm::vec4(0.6078,0.4627,0.3255,1);
    header->colors[Tile::grass] = glm::vec4(0.3373,0.4902,0.2745,1);
    header->colors[Tile::stone] = glm::vec4(0.6118,0.6353,0.7216,1);
    header->colors[Tile::sand] = glm::vec4(0.761,0.698,0.502,1);
    header->colors[Tile::shallow_water] = Color("74ccf4").rgba;
    header->colors[Tile::water] = Color("2389da").rgba;

    regions.push_back(vk::BufferCopy(0, 0, sizeof(Header)));

    int staging_counter = 0;

    auto& map = reg.ctx<MapManager>();
    auto& camera = reg.ctx<Camera>();

    /*
    auto corner_pos = map.getChunkPos(camera.getCorner());//-1;
    auto end_pos = map.getChunkPos(camera.getCorner() + camera.getSize());//+1;
    */
    
    auto corner_rpos = camera.fromScreenSpace(glm::vec2()) - 10.f;
    auto corner_pos = map.getChunkPos(corner_rpos);
    auto end_rpos = camera.fromScreenSpace(camera.getScreenSize()) + 10.f;
    auto end_pos = map.getChunkPos(end_rpos);
    end_pos.y++;

    header->corner_indices = corner_pos;
    header->chunk_length = end_pos.y - corner_pos.y + 1;

    data.num_chunks = 0;

    for(int x = corner_pos.x; x <= end_pos.x; x++) {
        for(int y = corner_pos.y; y <= end_pos.y; y++) {

            OPTICK_EVENT("MapUploadSys::tick::chunk");

            auto chunk_pos = glm::ivec2(x, y);

            Chunk* chunk = map.getChunk(chunk_pos);
            if(chunk == nullptr) chunk = map.generateChunk(chunk_pos);

            bool stored = false;

            int index = find_stored_chunk(chunk_pos);

            if(index != -1) stored = true;

            if(!stored) {

                index = find_storage_slot();

                RenderChunk* rchunk = reinterpret_cast<RenderChunk*>(header+1);

                for(int i = 0; i<Chunk::size; i++) {
                    for(int j = 0; j<Chunk::size; j++) {
                        Tile& tile = chunk->get(glm::vec2(i,j));
                        rchunk[staging_counter].tiles[i * Chunk::size + j] = {tile.terrain, tile.level};
                    }
                }

                regions.push_back(vk::BufferCopy(sizeof(Header) + staging_counter * sizeof(RenderChunk), sizeof(Header) + index * sizeof(RenderChunk), sizeof(RenderChunk)));

                staging_counter++;

                chunk->setUpdated(false);

            }
            
            glm::vec2 chunk_screen_coords = chunk_pos - corner_pos;
            int chunk_index_index = chunk_screen_coords.x * header->chunk_length + chunk_screen_coords.y;
            
            if(chunk_index_index >= max_chunks) {
                log(Level::warning) << "too many chunks\n";
                x = end_pos.x + 1;
                y = end_pos.y + 1;
                break;
            }
            
            header->chunk_indices[chunk_index_index] = index;

            StoredChunk& sc = stored_chunks[index];

            sc.position = chunk_pos;
            sc.stored = true;
            sc.chunk = chunk;
            sc.used = true;
            
            data.num_chunks++;

        }
    }

    for(int i = 0; i<stored_chunks.size(); i++) {
        stored_chunks[i].used = false;
    }

    transfer.copyBuffer(f.stagingBuffer, storageBuffer, regions);
    
    ImGui::Begin("Chunk updates", nullptr);
    
    ImGui::Text("%i", staging_counter);
    
    ImGui::End();

}


MapUploadSys::~MapUploadSys() {

    Context& ctx = *reg.ctx<Context*>();

    ctx.device->destroy(descPool);

    MapUploadData& data = reg.ctx<MapUploadData>();

    ctx.device->destroy(data.descLayout);

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
            /*|| (sc.position.x >= corner_pos.x && sc.position.x <= end_pos.x && sc.position.y >= corner_pos.y && sc.position.y <= end_pos.y)*/
            )) {
        storage_counter = (storage_counter+1)%stored_chunks.size();
        sc = stored_chunks[storage_counter];
    }
    int index = storage_counter;
    storage_counter++;
    return index;

}