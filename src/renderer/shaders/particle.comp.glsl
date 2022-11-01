#version 450

layout (local_size_x = 256) in;

const int CHUNK_SIZE = 32;
const int NUM_TYPES = 7;
const int MAX_CHUNKS = 10000; // MUST BE SAME AS IN MAP_UPLOAD

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

struct KeyValue {
    uint key;
    uint value;
};

layout(std430, set = 0, binding = 1) buffer HashMap {
    KeyValue slots[];
} map;

const uint MAX_NEW_PARTICLES = 100;

layout(std430, set = 0, binding = 2) readonly buffer NewParticles {
    Particle new_particles[MAX_NEW_PARTICLES];
};

struct Tile {
    int type;
    float height;
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

    vec2 pos = new_pos.xy;

    Tile t = getTile(new_pos.xy);

    float dhdx = getTile(pos - vec2(1,0)).height - getTile(pos + vec2(1,0)).height;
    float dhdy = getTile(pos - vec2(0,1)).height - getTile(pos + vec2(0,1)).height;
    vec3 norm = normalize(vec3(dhdx, dhdy, 1));

    if(new_pos.z < t.height + p.sphere.w) {
        new_pos.z = t.height + p.sphere.w;
        p.speed.xyz -= 2 * dot(p.speed.xyz, norm) * norm;
        p.speed.xyz *= 0.9;
    }
    p.sphere.xyz = new_pos;

    particles[particle_index] = p;
}
