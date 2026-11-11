#ifndef DY_RAY_SCENE_H
#define DY_RAY_SCENE_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"
#include "renderer/lighting/ray_meshes.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "logic/components/lighting_stats.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dy {

class Context;

// An acceleration structure and the buffer it lives in
struct Accel {
    VmaBuffer buffer;
    vk::AccelerationStructureKHR handle;
    vk::DeviceAddress address = 0;
};

// A chunk whose acceleration structure is in the scene the rays see (or that has no triangle)
struct ChunkLit {};

// What the rays see (docs/lighting.md) :
//  - a bottom level acceleration structure per chunk, built from the vertex buffer the marching cubes shader
//    fills, once its triangle count has come back from the GPU
//  - one per mesh the passes gave (RayMeshes) : the species of the plants, the models
//  - the top level one, built again every frame in the compute command buffer of the frame, so that the
//    graphics shaders trace the scene of their own frame : the chunks, the plants near a viewer, the models
//  - the geometry table (binding 5 of the view set) : what a shader needs to know about a hit, at
//    instanceCustomIndex + geometry index (RayGeometry of include/rays.glsl)
// Only exists with ray tracing (RayContext::enabled()).
class RayScene {
public:
    // Masks of the instances : a ray leaves some out
    enum Mask : uint32_t {
        Terrain = 1,
        Plants = 2,
        Static = 4,     // models that do not move
        Dynamic = 8,    // boxes, hands, animals
    };

    RayScene(Context& ctx, entt::registry& reg);
    ~RayScene();

    // Main thread, after the systems and the update of the passes, before the compute commands of the
    // frame are submitted
    void update();

private:
    // RayGeometry of the shaders, scalar layout
    struct GeometryEntry {
        uint64_t vertices = 0;      // address of the first vertex the indices count from
        uint64_t indices = 0;
        int32_t texture = -1;
        float alpha_cutoff = 0.f;
        glm::vec3 color = glm::vec3(0.5f);
        uint32_t kind = 0;          // RayMesh::Kind
        glm::vec3 transmission = glm::vec3(0.f);
        uint32_t pad = 0;
    };
    static_assert(sizeof(GeometryEntry) == 56);

    // One build of this frame. false : the scratch memory of the frame is spent, try again the next one
    bool queueBuild(Accel& out, vk::AccelerationStructureTypeKHR type, vk::BuildAccelerationStructureFlagsKHR flags,
        std::vector<vk::AccelerationStructureGeometryKHR> geometries, std::vector<uint32_t> primitives);
    void flushBuilds(vk::CommandBuffer command);

    void retire(Accel&& accel);
    void onChunkGone(entt::registry& reg, entt::entity entity);

    void updateChunks();
    void updateMeshes();
    void gatherInstances();
    void buildTop(vk::CommandBuffer command);

    uint32_t addGeometries(const std::vector<GeometryEntry>& entries);
    void addInstance(const glm::mat4& matrix, vk::DeviceAddress structure, uint32_t first_geometry, uint32_t mask);

    Context& ctx;
    entt::registry& reg;

    struct Build {
        vk::AccelerationStructureBuildGeometryInfoKHR info;
        std::vector<vk::AccelerationStructureGeometryKHR> geometries;
        std::vector<vk::AccelerationStructureBuildRangeInfoKHR> ranges;
    };
    std::vector<std::unique_ptr<Build>> builds;

    VmaBuffer scratch;
    vk::DeviceAddress scratch_address = 0;
    vk::DeviceSize scratch_size = 0, scratch_used = 0;

    struct Retired {
        Accel accel;
        VmaBuffer buffer;
        int frames = 0;
    };
    std::vector<Retired> retired;

    // Chunks
    struct ChunkAccel {
        Accel accel;
        uint32_t slot = 0;
    };
    std::unordered_map<entt::entity, ChunkAccel> chunks;

    // Meshes of the passes, by their index in RayMeshes
    struct MeshAccel {
        Accel accel;
        uint32_t first_geometry = 0;
        bool built = false;
    };
    std::vector<MeshAccel> meshes;

    // The geometry table : entries are only added, in place (the frames in flight do not know the new ones)
    VmaBuffer table;
    GeometryEntry* table_pointer = nullptr;
    uint32_t table_capacity = 0, table_count = 0;

    // Top level : one structure built again every frame, its instances in a buffer per compute command buffer
    Accel top;
    uint32_t top_capacity = 0;
    VmaBuffer top_scratch;
    vk::DeviceAddress top_scratch_address = 0;
    struct Instances {
        VmaBuffer buffer;
        vk::AccelerationStructureInstanceKHR* pointer = nullptr;
        vk::DeviceAddress address = 0;
        uint32_t capacity = 0;
    };
    std::vector<Instances> instance_buffers;
    std::vector<vk::AccelerationStructureInstanceKHR> instances;
    void createTop(uint32_t capacity);
};

}

#endif
