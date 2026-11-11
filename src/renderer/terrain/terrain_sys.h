#ifndef DY_TERRAIN_SYS_H
#define DY_TERRAIN_SYS_H

#include "logic/systems/system.h"

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace dy {

// Vertex written by the marching cubes shader, 32 bytes
struct TerrainVertex {
    glm::vec3 pos;
    glm::vec2 normal;   // sum of the triangles around the vertex (vertexNormal of marchingcubes.comp.glsl), octahedral, in -1..1 (decodeNormal of terrain.vert.glsl)
    glm::vec3 curve;    // how the ground bends : second derivatives of its height, xx, xz, zz (logic/terrain/ground_curve.h)
};

// GPU mesh slot of a chunk
class ChunkGPU {
public:
    uint32_t slot;
};
struct ChunkReady {}; // the mesh was computed in a submitted compute batch, drawable

// How many triangles the mesh of the chunk has : only the GPU knows when it is drawn, read back a few
// frames later where something needs it on the CPU (the acceleration structures of the lighting)
struct ChunkTriangles {
    uint32_t count = 0;
    uint32_t slot = 0;      // of the mesh that was counted
};

// Context variable read by the terrain and grass passes
struct TerrainBuffers {
    vk::Buffer indirect;
    vk::DeviceSize indirect_stride = 0;
    std::vector<vk::Buffer> vertices; // per slot
};

// Turns chunk densities into meshes on the GPU (marching cubes, recorded into the compute queue)
// and manages the per-chunk vertex buffers and indirect draw commands.
class TerrainSys : public System {
public:
    TerrainSys(entt::registry& reg);
    ~TerrainSys() override;
    const char* name() override { return "Terrain"; }
    void init() override;
    void tick(double dt) override;

    // Max chunks meshed per frame, the staging ring holds this many per compute frame
    static constexpr uint32_t max_per_frame = 8;
    static constexpr uint32_t max_slots = 2048;
    static constexpr uint32_t max_triangles = 1 << 15;

private:
    uint32_t allocateSlot();

    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout lookupLayout;
    vk::DescriptorSetLayout chunkLayout;
    vk::DescriptorSet lookupSet;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline pipeline;

    VmaBuffer lookupBuffer;
    vk::BufferView lookupView;

    VmaBuffer indirect;
    vk::DeviceSize indirect_stride;

    VmaBuffer staging;
    uint8_t* staging_pointer = nullptr;
    vk::DeviceSize staging_stride;

    struct Slot {
        VmaBuffer vertices;
        vk::DescriptorSet set;
    };
    std::vector<std::unique_ptr<Slot>> slots;
    std::vector<uint32_t> free_slots;

    // Slots of destroyed chunks may still be read by frames in flight
    struct PendingFree {
        uint32_t slot;
        uint64_t frame;
    };
    std::vector<PendingFree> pending_frees;

    // With ray tracing : the indirect commands of the meshes of a frame are copied here, a slot each,
    // and read once that frame is done
    VmaBuffer readback;
    const vk::DrawIndirectCommand* readback_pointer = nullptr;
    struct PendingCount {
        entt::entity entity;
        uint32_t slot;
        uint64_t frame;
    };
    std::vector<PendingCount> pending_counts;
    bool ray_tracing = false;
    uint64_t frame_counter = 0;
};

}

#endif
