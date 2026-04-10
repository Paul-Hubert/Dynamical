#ifndef MAP_UPLOAD_DATA_H
#define MAP_UPLOAD_DATA_H

#include "renderer/util/vk.h"
#include "logic/map/chunk.h"

namespace dy {

constexpr int max_chunks = 10000; // MUST CHANGE IN SHADER
constexpr int max_stored_chunks = 20000;

struct TileData {
    int type;
    float height;
};

struct KeyValue {
    uint32_t key;
    uint32_t value;
};

struct Header {
    glm::vec4 colors[Tile::Type::max];
    glm::ivec2 chunk_positions[max_chunks]; // per-instance chunk positions, grouped by LOD
    KeyValue chunk_indices[max_chunks];
};

struct RenderChunk {
    TileData tiles[Chunk::size*Chunk::size];
};

struct RenderObject {
    glm::vec4 sphere;
    glm::vec4 color;
};

struct RenderBuilding {
    glm::vec4 position;     // world x, y, z (terrain height), rotation_y
    glm::vec4 dimensions;   // width, depth, wall_height, roof_height
    glm::vec4 wall_color;   // RGBA
    glm::vec4 roof_color;   // RGBA
};

struct Particle {
    glm::vec4 sphere;
    glm::vec4 color;
    glm::vec4 speed;

    glm::vec3 pressure;
    float density;
    glm::vec3 new_pressure;
    float new_density;

    glm::vec4 viscosity;
    glm::vec4 new_viscosity;

};

struct MapUploadData {
    vk::DescriptorSetLayout mapLayout;
    vk::DescriptorSet mapSet;
    int num_chunks;
    int num_chunks_lod0 = 0;
    int num_chunks_lod1 = 0;
    int num_chunks_lod2 = 0;
    vk::Buffer objectBuffer;
    int num_objects;
    vk::Buffer buildingBuffer;
    int num_buildings;
    vk::Buffer particleBuffer;
    int num_particles;
};

}

#endif
