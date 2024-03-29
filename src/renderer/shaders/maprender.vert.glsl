#version 450

const int CHUNK_SIZE = 32;
const int NUM_TYPES = 7;
const int MAX_CHUNKS = 10000; // MUST BE SAME AS IN MAP_UPLOAD

layout(location = 0) out vec2 v_pos;
layout(location = 1) out vec3 v_normal;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(set = 0, binding = 0) uniform Camera {
    mat4 projection;
    mat4 view;
} camera;

struct Tile {
    int type;
    float height;
};

struct KeyValue {
    uint key;
    uint value;
};

layout(std430, set = 1, binding = 0) readonly buffer Map {
    vec4 colors[NUM_TYPES];
    ivec2 corner_indices;
    uint chunk_length;
    KeyValue chunk_indices[MAX_CHUNKS];
    Tile tiles[];
};



ivec2 ifloor(vec2 v) {
    return ivec2(floor(v));
}

uint hash(ivec2 chunk_pos) {
    return chunk_pos.x * (1<<16) + chunk_pos.y + 1;
}

Tile getTile(vec2 pos) {

    ivec2 ipos = ifloor(pos);

    ivec2 chunk_pos = ifloor(pos / CHUNK_SIZE);

    uint hash = hash(chunk_pos);

    uint slot = hash%MAX_CHUNKS;

    int index = -1;

    while(true) {
        if(chunk_indices[slot].key == hash) {
            index = int(chunk_indices[slot].value);
            break;
        } else if(chunk_indices[slot].key == 0) {
            break;
        }
        slot = (slot+1) % MAX_CHUNKS;
    }

    if(index == -1) {
        return Tile(0, 0);
    }
    
    ivec2 tile_space = ipos - chunk_pos * CHUNK_SIZE;

    Tile tile = tiles[index * CHUNK_SIZE * CHUNK_SIZE + tile_space.x * CHUNK_SIZE + tile_space.y];

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
    
    float dhdx = getTile(v_pos - vec2(1,0)).height - getTile(v_pos + vec2(1,0)).height;
    float dhdy = getTile(v_pos - vec2(0,1)).height - getTile(v_pos + vec2(0,1)).height;
    v_normal = normalize(vec3(dhdx, dhdy, -1));

    gl_Position = camera.projection * camera.view * vec4(v_pos, tile.height, 1.0f);
    
}
