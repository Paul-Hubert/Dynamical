#version 450
#extension GL_EXT_debug_printf : enable

layout (local_size_x = 256) in;

struct Particle {
    vec4 sphere;
    vec4 speed;
    vec4 color;
};

layout( push_constant ) uniform constants
{
    uint particle_count;
    uint new_particle_count;
};

layout(std430, set = 0, binding = 0) buffer Particles {
    Particle particles[];
};

struct Slot {
    uint key;
    uint value;
};

layout(std430, set = 0, binding = 1) buffer HashMap {
    uint slots[];
} map;

const uint MAX_NEW_PARTICLES = 100;

layout(std430, set = 0, binding = 2) readonly buffer NewParticles {
    Particle new_particles[MAX_NEW_PARTICLES];
};

const int CHUNK_SIZE = 32;
const int NUM_TYPES = 7;
const int MAX_CHUNKS = 5000; // MUST BE SAME AS IN MAP_UPLOAD

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

    if(indices.x < 0 || indices.y < 0 || indices.x * chunk_length + indices.y >= MAX_CHUNKS) return Tile(0, 0);

    int chunk_index = chunk_indices[indices.x * chunk_length + indices.y];
    debugPrintfEXT("chonker is %f", chunk_index);

    ivec2 tile_space = ipos - real_indices * CHUNK_SIZE;

    Tile tile = tiles[chunk_index * CHUNK_SIZE * CHUNK_SIZE + tile_space.x * CHUNK_SIZE + tile_space.y];

    if(tile.type == 5) {
        tile.height = 0.0f;
    }

    return Tile(0, 10);

}

void main()
{
    uint particle_index = gl_GlobalInvocationID.x;

    Particle p;
    if(particle_index >= particle_count - new_particle_count) {
        p = new_particles[particle_count - particle_index - 1];
    } else {
        p = particles[particle_index];
    }


    //Gravité, plus tard on ajoutera une force dépendante des autres particules
    p.speed.z -= 0.1;

    vec3 new_pos = p.sphere.xyz + p.speed.xyz;

    Tile t = getTile(p.sphere.xy);

    //@TODO fixme!
    vec3 norm = vec3(0,0,1);

    if(new_pos.z - p.sphere.w < t.height) {
        new_pos.z = t.height + p.sphere.w;
        p.speed.xyz -= 2 * dot(p.speed.xyz, norm) * norm;
    }
    p.sphere.xyz = new_pos;

    particles[particle_index] = p;
}
