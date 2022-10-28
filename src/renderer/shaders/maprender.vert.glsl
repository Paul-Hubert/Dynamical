#version 450

layout(constant_id = 0) const int CHUNK_SIZE = 32;
layout(constant_id = 1) const int NUM_TYPES = 7;
layout(constant_id = 2) const int MAX_CHUNKS = 2000; // MUST BE SAME AS IN MAP_UPLOAD

layout(location = 0) out vec2 v_pos;
layout(location = 1) out vec3 v_normal;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(set = 0, binding = 0) uniform Camera {
    mat4 projection;
    mat4 view;
};

struct Tile {
    int type;
    float height;
};

layout(std430, set = 1, binding = 0) readonly buffer Map {
    vec4 colors[NUM_TYPES];
    ivec2 corner_indices;
    int chunk_length;
    int chunk_indices[MAX_CHUNKS];
    Tile tiles[];
};



ivec2 ifloor(vec2 v) {
    return ivec2(floor(v));
}

Tile getTile(vec2 pos) {

    ivec2 ipos = ifloor(pos);

    ivec2 real_indices = ifloor(pos / CHUNK_SIZE);

    ivec2 indices = real_indices - corner_indices;

    int chunk_index = chunk_indices[indices.x * chunk_length + indices.y];
    
    ivec2 tile_space = ipos - real_indices * CHUNK_SIZE;

    Tile tile = tiles[chunk_index * CHUNK_SIZE * CHUNK_SIZE + tile_space.x * CHUNK_SIZE + tile_space.y];

    if(tile.type == 5) {
        tile.height = 0.0f;
    }
    
    return tile;

}

vec2 diagonal(vec2 pos) {
    return pos + (pos - round(pos));
}

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}


void main() {

    int gridSize = CHUNK_SIZE + 1;
    ivec2 vertex_coords = ivec2(gl_VertexIndex%gridSize, int(gl_VertexIndex/gridSize));
    ivec2 chunk_coords = ivec2(int(gl_InstanceIndex/chunk_length), gl_InstanceIndex%chunk_length);
    v_pos = (corner_indices + chunk_coords) * CHUNK_SIZE + vertex_coords;
    
    Tile tile = getTile(v_pos);
    
    if(tile.type == 5) tile.height = 0.0f;
    
    /*
    v_color = colors[tile.type].rgb;
    
    for(int i = 0; i<NUM_TYPES; i++) {
        v_types[i] = 0;
    }
    v_types[tile.type] = 1;
    */
    
    float dhdx = getTile(v_pos - vec2(1,0)).height - getTile(v_pos + vec2(1,0)).height;
    float dhdy = getTile(v_pos - vec2(0,1)).height - getTile(v_pos + vec2(0,1)).height;
    v_normal = normalize(vec3(dhdx, dhdy, -2));
    
    
    gl_Position = projection * view * vec4(v_pos, -tile.height*4.f, 1.0f);
    gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
    
}
