#ifndef MAP_UPLOAD_SYS_H
#define MAP_UPLOAD_SYS_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include "imgui.h"

#include "system.h"

#include <entt/entt.hpp>

#include <glm/glm.hpp>

#include "logic/map/chunk.h"

#include <memory>

namespace dy {
    
    constexpr int max_chunks = 10000;
    constexpr int max_stored_chunks = 50000;

    struct Header {
        glm::vec4 colors[Tile::Type::max];
        glm::ivec2 corner_indices;
        int chunk_length;
        int chunk_indices[max_chunks];
    };

    struct RenderChunk {
        int tiles[Chunk::size*Chunk::size];
    };

    struct MapUploadData {
        vk::DescriptorSetLayout descLayout;
        vk::DescriptorSet descSet;
        int num_chunks;
    };

    class Context;
    class Renderpass;

    class MapUploadSys : public System {
    public:
        MapUploadSys(entt::registry& reg);
        ~MapUploadSys() override;

        const char* name() override {
            return "MapUpload";
        }

        void tick(float dt) override;

    private:

        vk::DescriptorPool descPool;

        struct per_frame {
            VmaBuffer stagingBuffer;
            void* stagingPointer;
        };
        std::vector<per_frame> per_frame;

        struct StoredChunk {
            glm::ivec2 position; // in chunk space
            bool used = false;
            bool stored = false;
            Chunk* chunk;
        };
        std::vector<StoredChunk> stored_chunks;
        int storage_counter = 0;

        VmaBuffer storageBuffer;

    };

}

#endif


