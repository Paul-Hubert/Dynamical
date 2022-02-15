#include "map_upload.h"

#include "renderer/context/context.h"

#include "imgui.h"
#include <string.h>

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
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment)
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

    auto corner_pos = map.getChunkPos(camera.corner)-1;
    auto end_pos = map.getChunkPos(camera.corner + camera.size)+1;

    header->corner_indices = corner_pos;
    header->chunk_length = end_pos.y - corner_pos.y + 1;
    int chunk_indices_counter = 0;

    for(int x = corner_pos.x; x <= end_pos.x; x++) {
        for(int y = corner_pos.y; y <= end_pos.y; y++) {

            OPTICK_EVENT("MapRenderSys::tick::chunk");

            auto pos = glm::ivec2(x, y);

            bool stored = false;
            for(int i = 0; i<stored_chunks.size(); i++) {
                auto& sc = stored_chunks[i];
                if(sc.stored && sc.position == pos) {
                    if(sc.chunk->isUpdated()) {
                        sc.stored = false;
                        break;
                    }
                    stored = true;
                    header->chunk_indices[chunk_indices_counter] = i;
                    chunk_indices_counter++;
                    sc.used = true;
                    break;
                }
            }



            if(!stored) {

                int index;

                if(stored_chunks.size() >= max_stored_chunks) {
                    auto& sc = stored_chunks[storage_counter];
                    while(
                            sc.stored
                            && (sc.used
                            || (sc.position.x >= corner_pos.x && sc.position.x <= end_pos.x
                            && sc.position.y >= corner_pos.y && sc.position.y <= end_pos.y))
                            ) {
                        storage_counter = (storage_counter+1)%max_stored_chunks;
                        sc = stored_chunks[storage_counter];
                    }
                    index = storage_counter;
                    storage_counter = (storage_counter+1)%max_stored_chunks;
                } else {
                    index = stored_chunks.size();
                    stored_chunks.push_back(StoredChunk{pos});
                    storage_counter = (storage_counter+1)%max_stored_chunks;
                }

                Chunk* chunk = map.getChunk(pos);
                if(chunk == nullptr) chunk = map.generateChunk(pos);

                RenderChunk* rchunk = reinterpret_cast<RenderChunk*>(header+1);

                for(int i = 0; i<Chunk::size; i++) {
                    for(int j = 0; j<Chunk::size; j++) {
                        rchunk[staging_counter].tiles[i * Chunk::size + j] = chunk->get(glm::vec2(i,j)).terrain;
                    }
                }

                regions.push_back(vk::BufferCopy(sizeof(Header) + staging_counter * sizeof(RenderChunk), sizeof(Header) + index * sizeof(RenderChunk), sizeof(RenderChunk)));

                header->chunk_indices[chunk_indices_counter] = index;
                chunk_indices_counter++;


                stored_chunks[index].position = pos;
                stored_chunks[index].used = true;
                stored_chunks[index].stored = true;
                stored_chunks[index].chunk = chunk;

                staging_counter++;

                chunk->setUpdated(false);

            }

            if(chunk_indices_counter >= max_chunks) {
                log(Level::warning) << "too many chunks\n";
                x = end_pos.x + 1;
                y = end_pos.y + 1;
                break;
            }

        }
    }

    for(int i = 0; i<stored_chunks.size(); i++) {
        stored_chunks[i].used = false;
    }

    transfer.copyBuffer(f.stagingBuffer, storageBuffer, regions);

}


MapUploadSys::~MapUploadSys() {

    Context& ctx = *reg.ctx<Context*>();

    ctx.device->destroy(descPool);

    MapUploadData& data = reg.ctx<MapUploadData>();

    ctx.device->destroy(data.descLayout);

}


