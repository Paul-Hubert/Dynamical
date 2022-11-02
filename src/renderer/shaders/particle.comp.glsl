#version 450

layout (local_size_x = 1) in;

const float PI = 3.1415926535897932384626433832795;
const int CHUNK_SIZE = 32;
const int NUM_TYPES = 7;
const int MAX_CHUNKS = 10000; // MUST BE SAME AS IN MAP_UPLOAD

const float GLOBAL_DENSITY = 1000;
const float STIFFNESS = 1;

const float SLOT_SIZE = 5;

const float GRAVITY = 9;
const float FRICTION = 0.9;

const float PARTICLE_MASS = 1;
const float PARTICLE_MASS_P2 = PARTICLE_MASS*PARTICLE_MASS;

const float VISCOSITY = 1;

const float KERNEL_RADIUS = 5;
const float KERNEL_RADIUS_P2 = KERNEL_RADIUS*KERNEL_RADIUS;
const float KERNEL_RADIUS_P6 = KERNEL_RADIUS_P2*KERNEL_RADIUS_P2*KERNEL_RADIUS_P2;
const float KERNEL_RADIUS_P9 = KERNEL_RADIUS_P6*KERNEL_RADIUS_P2*KERNEL_RADIUS;

struct Particle {
    vec4 sphere;
    vec4 color;
    vec4 speed;

    vec3 pressure;
    float density;

    vec3 new_pressure;
    float new_density;

    vec3 viscosity;
    float a;

    vec3 new_viscosity;
    float b;
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

float smooth_density(float distance) {
    float ret = (KERNEL_RADIUS_P2-(distance*distance));
    ret = ret * ret * ret;
    ret *= 315/(64*PI*KERNEL_RADIUS_P9);
    return ret;
}

float smooth_viscosity(float distance) {
    return 45*(KERNEL_RADIUS-distance)/(PI*KERNEL_RADIUS_P6);
}

float smooth_pressure_grad(float distance) {
    float hd = (KERNEL_RADIUS-distance);
    float scal = -45. / (PI*KERNEL_RADIUS_P6 * distance) * hd*hd;
    return scal;
}

uint morton(ivec3 pos) {
    //0x80000 so that it's never zero
    return 0x800000 | part1by2(pos.x) | (part1by2(pos.y) << 1) | (part1by2(pos.z) << 2);
}

ivec3 round_vec(vec3 pos) {
    return ivec3(int(pos.x+0.5),int(pos.y+0.5),int(pos.z+0.5));
}

ivec3 get_indices(vec3 pos) {
    return round_vec(pos/SLOT_SIZE);
}

void insert_particle(ivec3 pos, uint particle_index) {
    uint code = morton(pos);
    uint slot = code & (HASHMAP_SLOTS-1);

    while(true) {
        uint prev = atomicCompSwap(map.slots[slot].key, HASHMAP_EMPTY, code);
        if(prev == HASHMAP_EMPTY) {
            map.slots[slot].value = particle_index;
            break;
        }
        slot = (slot + 1) & (HASHMAP_SLOTS-1);
    }
}

float pressure(float density) {
    return STIFFNESS*(density-GLOBAL_DENSITY);
}

void interaction(uint p_index, inout Particle p, uint other) {
    if(p_index != other) {
        Particle o = particles[other];
        vec3 diff = p.sphere.xyz - o.sphere.xyz;
        float distance = sqrt(dot(diff, diff));

        if(distance != 0 && distance <= KERNEL_RADIUS) {
            p.new_density += PARTICLE_MASS*smooth_density(distance);

            p.new_pressure += PARTICLE_MASS * (pressure(p.density)+pressure(o.density)) / (2*o.density) * smooth_pressure_grad(distance) * diff;
            p.new_viscosity += PARTICLE_MASS * (o.speed.xyz-p.speed.xyz) / o.density * smooth_viscosity(distance);
        }
    }
}

/*
void interaction(uint p_index, inout Particle p, uint other) {
    if(p_index != other) {
        Particle o = particles[other];
        vec3 norm = p.sphere.xyz - o.sphere.xyz;
        float distance = sqrt(dot(norm, norm));
        float min_distance = p.sphere.w + o.sphere.w;
        norm = norm/distance;
        if(distance < min_distance) {
            vec3 pusher = norm * (distance-min_distance)/2;
            o.sphere.xyz += pusher;
            p.sphere.xyz -= pusher;

            vec3 vn = dot(p.speed.xyz, norm) * norm;
            vec3 vno = dot(o.speed.xyz, norm) * norm;

            p.speed.xyz = p.speed.xyz - vn + vno;
            o.speed.xyz = o.speed.xyz - vno + vn;

            p.speed.xyz *= FRICTION;
            o.speed.xyz *= FRICTION;
            particles[other] = o;
        }
    }
}
*/

void lookup_and_apply(uint p_index, inout Particle p, ivec3 pos) {
    uint code = morton(pos);
    uint slot = code & (HASHMAP_SLOTS-1);

    while (true) {
        if (map.slots[slot].key == code) {
            interaction(p_index, p, map.slots[slot].value);
        } else if(map.slots[slot].key == HASHMAP_EMPTY) {
            break;
        }
        slot = (slot + 1) & (HASHMAP_SLOTS-1);
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

float getSmoothHeight(vec2 pos) {
    Tile t = getTile(pos);

    float hoo = t.height;
    float hoi = getTile(pos + vec2(0,1)).height;
    float hio = getTile(pos + vec2(1,0)).height;
    float hii = getTile(pos + vec2(1,1)).height;

    vec2 tile_space = pos - floor(pos);

    float hxo = hoo * (1.-tile_space.x) + hio * tile_space.x;
    float hxi = hoi * (1.-tile_space.x) + hii * tile_space.x;
    float rheight = hxo * (1.-tile_space.y) + hxi * tile_space.y;

    return rheight;
}

vec3 getDerivative(vec2 pos) {
    float hoo = getTile(pos).height;
    float hoi = getTile(pos + vec2(0,1)).height;
    float hio = getTile(pos + vec2(1,0)).height;

    float dhdx = hoo - hio;
    float dhdy = hoo - hoi;
    vec3 norm = normalize(vec3(dhdx, dhdy, 1));
    return norm;
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

    p.density = p.new_density;
    p.new_density = 1.0;

    p.pressure = p.new_pressure;
    p.new_pressure = vec3(0,0,0);

    p.viscosity = p.new_viscosity;
    p.new_viscosity = vec3(0,0,0);

    insert_particle(get_indices(p.sphere.xyz), particle_index);
    barrier();


    //Gravité, plus tard on ajoutera une force dépendante des autres particules
    neighbours_xyz(particle_index, p, get_indices(p.sphere.xyz));
    p.new_pressure *= -PARTICLE_MASS/p.density;
    p.new_viscosity *= PARTICLE_MASS * VISCOSITY;

    p.speed.z -= 0.1;
    p.speed.xyz += (p.new_viscosity + p.new_pressure) / PARTICLE_MASS;

    vec3 new_pos = p.sphere.xyz + p.speed.xyz;

    vec2 pos = new_pos.xy;

    vec3 norm = getDerivative(pos);

    float rheight = getSmoothHeight(pos);

    float dist = -dot(vec3(0, 0, rheight - new_pos.z), norm);

    if(dist < p.sphere.w) {
        new_pos += norm * (p.sphere.w - dist);
        p.speed.xyz -= 2 * dot(p.speed.xyz, norm) * norm;
        p.speed.xyz *= FRICTION;
    }
    p.sphere.xyz = new_pos;

    particles[particle_index] = p;
}
