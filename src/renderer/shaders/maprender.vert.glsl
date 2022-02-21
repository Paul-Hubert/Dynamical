#version 450

layout(constant_id = 0) const int CHUNK_SIZE = 32;
layout(constant_id = 1) const int NUM_TYPES = 7;
layout(constant_id = 2) const int MAX_CHUNKS = 10000; // MUST BE SAME AS IN MAP_UPLOAD

layout(location = 0) out vec2 v_pos;
//layout(location = 1) out vec4 v_color;
layout(location = 2) out float v_types[NUM_TYPES];

out gl_PerVertex {
    vec4 gl_Position;
};

layout(set = 0, binding = 0) uniform Camera {
    mat4 projection;
    mat4 view;
    vec2 position;
    vec2 size;
};

struct Tile {
    int type;
    //float height;
};

layout(std430, set = 1, binding = 0) readonly buffer Map {
    vec4 colors[NUM_TYPES];
    ivec2 corner_indices;
    int chunk_length;
    int chunk_indices[MAX_CHUNKS];
    Tile tiles[];
};

void main() {

    int gridSize = CHUNK_SIZE + 1;
    ivec2 vertex_coords = ivec2(gl_VertexIndex%gridSize, int(gl_VertexIndex/gridSize));
    ivec2 chunk_coords = ivec2(int(gl_InstanceIndex/chunk_length), gl_InstanceIndex%chunk_length);
    v_pos = (corner_indices + chunk_coords) * CHUNK_SIZE + vertex_coords;
    gl_Position = projection * view * vec4(v_pos, 0.0f, 1.0f);
    
    int chunk_index_index = chunk_coords.x * chunk_length + chunk_coords.y;    
    int chunk_index = chunk_indices[chunk_index_index];
    Tile tile = tiles[chunk_index * CHUNK_SIZE * CHUNK_SIZE + vertex_coords.x * CHUNK_SIZE + vertex_coords.y];
    //v_color = colors[tile.type];
    
    for(int i = 0; i<NUM_TYPES; i++) {
        v_types[i] = 0;
    }
    v_types[tile.type] = 1;
    
}
