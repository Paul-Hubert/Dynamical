#version 450

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 outColor;

layout(constant_id = 0) const int CHUNK_SIZE = 32;
layout(constant_id = 1) const int NUM_TYPES = 7;
layout(constant_id = 2) const int MAX_CHUNKS = 10000;

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

int getType(vec2 pos) {

    ivec2 ipos = ifloor(pos);

    ivec2 real_indices = ifloor(pos / CHUNK_SIZE);

    ivec2 indices = real_indices - corner_indices;

    if(indices.x * chunk_length + indices.y >= MAX_CHUNKS) return -1;

    ivec2 tile_space = ipos - real_indices * CHUNK_SIZE;

    int chunk_index = chunk_indices[indices.x * chunk_length + indices.y];
    
    int type = tiles[chunk_index * CHUNK_SIZE * CHUNK_SIZE + tile_space.x * CHUNK_SIZE + tile_space.y];
    
    return type;
    
}

vec2 diagonal(vec2 pos) {
    return pos + (pos - round(pos));
}

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    
    vec2 pos = position + v_uv * size;
    ivec2 ipos = ifloor(pos);
    
    int type = getType(pos);
    vec2 rond = round(pos);
    vec2 dist = rond - pos;
    vec2 diag = rond + dist;
    int xadj = getType(vec2(diag.x, pos.y));
    int yadj = getType(vec2(pos.x, diag.y));
    int dadj = getType(diag);
    
    int trespass = 0;
    if(xadj == yadj) {
        if(type != dadj) {trespass = 1;}
        else if(type != xadj) {
            float ran = rand(rond) - 0.5;
            ran *= sign(dist.x) * sign(dist.y);
            if(ran >= 0) trespass = 1;
        }
    }
    
    if(trespass == 1 && length(pos - (ifloor(pos) + 0.5)) > 0.5)
            type = xadj;
    
    vec4 color = colors[type];
    
    outColor = color;

    ivec2 real_indices = ifloor(pos / CHUNK_SIZE);

    ivec2 indices = real_indices - corner_indices;

    ivec2 tile_space = ipos - real_indices * CHUNK_SIZE;

    int chunk_index = chunk_indices[indices.x * chunk_length + indices.y];

    //outColor.rgb = vec3(float(chunk_index) / 100.0);

    outColor.a = 1.0;
    
}
