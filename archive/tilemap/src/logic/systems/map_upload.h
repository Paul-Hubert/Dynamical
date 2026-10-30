#ifndef MAP_UPLOAD_SYS_H
#define MAP_UPLOAD_SYS_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include "imgui.h"

#include "system.h"

#include <entt/entt.hpp>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include "logic/map/chunk.h"

#include "logic/components/map_upload_data.h"

#include <memory>

namespace dy {




    class Context;
    class Renderpass;

    class MapUploadSys : public System {
    public:
        MapUploadSys(entt::registry& reg);
        ~MapUploadSys() override;

        const char* name() override {
            return "MapUpload";
        }

        void tick(double dt) override;

    private:

        int find_stored_chunk(glm::ivec2 chunk_pos);
        int find_storage_slot();
        uint32_t hash(glm::ivec2 chunk_pos);
        void insert_chunk(Header* header, glm::ivec2 chunk_pos, int index);

        vk::DescriptorPool descPool;

        struct per_frame {
            VmaBuffer stagingBuffer;
            Header* stagingPointer;
            VmaBuffer objectBuffer;
            RenderObject* objectPointer;
        };
        std::vector<per_frame> per_frame;

        struct StoredChunk {
            glm::ivec2 position; // in chunk space
            bool used = false;
            bool stored = false;
            Chunk* chunk = nullptr;
        };
        std::vector<StoredChunk> stored_chunks;
        int storage_counter = 0;

        VmaBuffer storageBuffer;

    };

}

#endif


