#version 450

layout(location = 0) in vec2 v_pos;
layout(location = 1) in vec4 v_color;

layout(location = 0) out vec4 outColor;

layout(constant_id = 0) const int CHUNK_SIZE = 32;
layout(constant_id = 1) const int NUM_TYPES = 7;
layout(constant_id = 2) const int MAX_CHUNKS = 10000; // MUST BE SAME AS IN MAP_UPLOAD

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

ivec2 ifloor(vec2 v) {
    return ivec2(floor(v));
}

Tile getTile(vec2 pos) {

    ivec2 ipos = ifloor(pos);

    ivec2 real_indices = ifloor(pos / CHUNK_SIZE);

    ivec2 indices = real_indices - corner_indices;

    if(indices.x * chunk_length + indices.y >= MAX_CHUNKS) discard;

    ivec2 tile_space = ipos - real_indices * CHUNK_SIZE;

    int chunk_index = chunk_indices[indices.x * chunk_length + indices.y];

    Tile tile = tiles[chunk_index * CHUNK_SIZE * CHUNK_SIZE + tile_space.x * CHUNK_SIZE + tile_space.y];

    return tile;

}

vec2 diagonal(vec2 pos) {
    return pos + (pos - round(pos));
}

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    
    outColor = v_color;

    /*

    vec2 pos = v_pos;
    ivec2 ipos = ifloor(pos);
    
    Tile tile = getTile(pos);
    vec2 rond = round(pos);
    vec2 dist = rond - pos;
    vec2 diag = rond + dist;
    Tile xadj = getTile(vec2(diag.x, pos.y));
    Tile yadj = getTile(vec2(pos.x, diag.y));
    Tile dadj = getTile(diag);
    
    int trespass = 0;
    if(xadj.type == yadj.type) {
        if(tile.type != dadj.type) {trespass = 1;}
        else if(tile.type != xadj.type) {
            float ran = rand(rond) - 0.5;
            ran *= sign(dist.x) * sign(dist.y);
            if(ran >= 0) trespass = 1;
        }
    }
    
    if(trespass == 1 && length(pos - (ifloor(pos) + 0.5)) > 0.5)
            tile.type = xadj.type;
    
    vec4 color = colors[tile.type];
    
    outColor = color;

    ivec2 real_indices = ifloor(pos / CHUNK_SIZE);

    ivec2 indices = real_indices - corner_indices;

    ivec2 tile_space = ipos - real_indices * CHUNK_SIZE;

    int chunk_index = chunk_indices[indices.x * chunk_length + indices.y];

    //outColor.rgb = vec3(float(indices.x * chunk_length + indices.y) / MAX_CHUNKS);

    outColor.a = 1.0;
    */
    
}
