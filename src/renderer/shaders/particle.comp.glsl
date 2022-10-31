#version 450

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

layout(std140, set = 0, binding = 2) uniform NewParticles {
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

void main()
{
    uint particle_index = gl_GlobalInvocationID.x;
    if(particle_index < new_particle_count) {
        particles[particle_count - new_particle_count + particle_index] = new_particles[particle_index];
    }

}
