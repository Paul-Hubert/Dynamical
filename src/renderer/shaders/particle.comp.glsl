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

//MUST be the same as in particle_simulation.h
const uint HASHMAP_SLOTS = 131072;
const uint HASHMAP_EMPTY = 0;
layout(std430, set = 0, binding = 1) buffer HashMap {
    KeyValue slots[HASHMAP_SLOTS];
} map;
é
const uint MAX_NEW_PARTICLES = 100;

layout(std430, set = 0, binding = 3) readonly buffer NewParticles {
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

// taken from
// https://gist.github.com/franjaviersans/885c136932ef37d8905a6433d0828be6
uint part1by2(uint n){
    n = n & 0x000003ff;
    n = (n ^ (n << 16)) & 0xff0000ff;
    n = (n ^ (n << 8)) & 0x0300f00f;
    n = (n ^ (n << 4)) & 0x030c30c3;
    n = (n ^ (n << 2)) & 0x09249249;
    return n;
}

uint morton(ivec3 pos) {
    //0x80000 so that it's never zero
    return 0x800000 | part1by2(pos.x) | (part1by2(pos.y) << 1) | (part1by2(pos.z) << 2);
}

ivec3 round_vec(vec3 pos) {
    return ivec3(int(pos.x+0.5),int(pos.y+0.5),int(pos.z+0.5));
}

void insert_particle(vec3 pos, uint particle_index) {
    uint code = morton(round_vec(pos));
    uint slot = code % HASHMAP_SLOTS;

    while(true) {
        uint prev = atomicCompSwap(map.slots[slot].key, HASHMAP_EMPTY, code);
        if(prev == HASHMAP_EMPTY) {
            map.slots[slot].value = particle_index;
            break;
        }
        //@TODO replace modulo by bitwise operator for performance
        slot = (slot + 1) % HASHMAP_SLOTS;
    }
}

void interaction(uint p_index, inout Particle p, uint other) {
    if(p_index != other) {
        Particle o = particles[other];
        vec3 norm = p.sphere.xyz - o.sphere.xyz;
        float distance = dot(norm, norm);
        float min_distance = p.sphere.w + o.sphere.w;
        if(distance < min_distance) {
            vec3 pusher = normalize(norm) * (distance-min_distance)/2;
            o.sphere.xyz += pusher;
            p.sphere.xyz -= pusher;

            p.speed.xyz -= 2 * dot(p.speed.xyz, norm) * norm;
            o.speed.xyz += 2 * dot(p.speed.xyz, norm) * norm;
            particles[other] = o;
        }
    }
}

void lookup_and_apply(uint p_index, inout Particle p, ivec3 pos) {
    uint code = morton(pos);
    uint slot = (code % HASHMAP_SLOTS) + 1;

    while (true) {
        if (map.slots[slot].key == code) {
            interaction(p_index, p, map.slots[slot].value);
        } else if(map.slots[slot].key == HASHMAP_EMPTY) {
            break;
        }
        slot = (slot + 1) % HASHMAP_SLOTS;
    }
}

uint hash_chunk(ivec2 chunk_pos) {
    return chunk_pos.x * (1<<16) + chunk_pos.y + 1;
}

Tile getTile(vec2 pos) {

    ivec2 ipos = ifloor(pos);

    ivec2 chunk_pos = ifloor(pos / CHUNK_SIZE);

    uint hash = hash_chunk(chunk_pos);

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

void neighbours_z(uint p_index, inout Particle p, ivec3 ipos) {
    lookup_and_apply(p_index, p, ipos);
    ipos.z += 1;
    lookup_and_apply(p_index, p, ipos);
}

void neighbours_yz(uint p_index, inout Particle p, ivec3 ipos) {
    neighbours_z(p_index, p, ipos);
    ipos.y += 1;
    neighbours_z(p_index, p, ipos);
}

void neighbours_xyz(uint p_index, inout Particle p, ivec3 ipos) {
    neighbours_yz(p_index, p, ipos);
    ipos.x += 1;
    neighbours_yz(p_index, p, ipos);
}

void main()
{
    uint particle_index = gl_GlobalInvocationID.x;

    Particle p;
    if(particle_index >= particle_count - new_particle_count) {
        p = new_particles[particle_count - particle_index - 1];
        particles[particle_index] = p;
    } else {
        p = particles[particle_index];
    }

    insert_particle(p.sphere.xyz, particle_index);
    barrier();


    //Gravité, plus tard on ajoutera une force dépendante des autres particules
    p.speed.z -= 0.1;

    neighbours_xyz(particle_index, p, round_vec(p.sphere.xyz));

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
