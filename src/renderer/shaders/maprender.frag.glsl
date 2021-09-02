#version 450

layout (location = 0) in vec2 v_uv;

layout(location = 0) out vec4 outColor;

layout (constant_id = 0) const int CHUNK_SIZE = 32;
layout (constant_id = 1) const int NUM_TYPES = 7;
layout (constant_id = 2) const int MAX_CHUNKS = 5000;

layout(set = 0, binding = 0) uniform Camera {
    vec2 position;
    vec2 size;
};

layout(std430, set = 1, binding = 0) readonly buffer Map {
    vec4 colors[NUM_TYPES];
    ivec2 corner_indices;
    int chunk_length;
    int chunk_indices[MAX_CHUNKS];
    int tiles[];
};

ivec2 ifloor(vec2 v) {
    return ivec2(floor(v));
}

void main() {
    
    vec2 pos = position + v_uv * size;
    ivec2 ipos = ifloor(pos);
    
    ivec2 real_indices = ifloor(pos / CHUNK_SIZE);
    
    ivec2 indices = real_indices - corner_indices;
    
    if(indices.x * chunk_length + indices.y >= MAX_CHUNKS) discard;
    
    ivec2 tile_space = ipos - real_indices * CHUNK_SIZE;
    
    int chunk_index = chunk_indices[indices.x * chunk_length + indices.y];
    
    int type = tiles[chunk_index * CHUNK_SIZE * CHUNK_SIZE + tile_space.x * CHUNK_SIZE + tile_space.y];
    
    vec4 color = colors[type];
    
    outColor = color;
    
    //outColor.r = abs(sin(real_indices.x * real_indices.y));
    //outColor = vec4(float(tile_space.x * CHUNK_SIZE + tile_space.y) / CHUNK_SIZE / CHUNK_SIZE);
    //outColor.r = float(corner_indices.x * chunk_length + corner_indices.y) / 32;
    //outColor = vec4(float(chunk_index) / 100);
    //outColor = vec4(type);
    //outColor = vec4(indices.x
    
    //outColor.r = real_indices.y /10.;
    
    outColor.a = 1.0;
    
}
