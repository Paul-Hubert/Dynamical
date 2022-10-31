#ifndef MAP_UPLOAD_DATA_H
#define MAP_UPLOAD_DATA_H

#include "renderer/util/vk.h"
#include "logic/map/chunk.h"

namespace dy {

constexpr int max_chunks = 5000; // MUST CHANGE IN SHADER
constexpr int max_stored_chunks = 10000;

struct TileData {
    int type;
    float height;
};

struct Header {
    glm::vec4 colors[Tile::Type::max];
    glm::ivec2 corner_indices;
    int chunk_length;
    int chunk_indices[max_chunks];
};

struct RenderChunk {
    TileData tiles[Chunk::size*Chunk::size];
};

struct RenderObject {
    glm::vec4 sphere;
    glm::vec4 color;
};

struct Particle {
    glm::vec4 sphere;
    glm::vec4 speed;
    glm::vec4 color;
};

struct MapUploadData {
    vk::DescriptorSetLayout mapLayout;
    vk::DescriptorSet mapSet;
    int num_chunks;
    vk::Buffer objectBuffer;
    int num_objects;
    vk::Buffer particleBuffer;
    int num_particles;
};

}

#endif
