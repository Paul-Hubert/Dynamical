#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

// The animals near the viewers cast shadows (docs/lighting.md) : the positions of one of them in its pose,
// in the space of its model, written where the acceleration structure of the animal is built from. The
// same skinning as animal.vert.glsl, an invocation a vertex. Buffers come by their address

layout(local_size_x = 64) in;

// AnimalVertex, 36 bytes
struct AnimalVertex {
    vec3 position;
    uint normal;
    vec2 uv;
    uint joints_low, joints_high;   // four joints of 16 bits
    uint weights;                   // four weights of 8 bits, their sum 255
};

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Vertices {
    AnimalVertex v[];
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Joints {
    mat4 m[];
};
layout(buffer_reference, scalar, buffer_reference_align = 4) writeonly buffer Skinned {
    vec4 p[];
};

layout(push_constant, scalar) uniform Args {
    Vertices vertices;      // of the species
    Joints joints;          // of the frame : bind pose to pose, in model space
    Skinned skinned;
    uint first_vertex;      // of the level that casts the shadow
    uint count;
    uint first_joint;       // of the animal
    uint first_out;
};

void main() {

    uint i = gl_GlobalInvocationID.x;
    if(i >= count) return;

    AnimalVertex vertex = vertices.v[first_vertex + i];
    uvec4 joint = uvec4(vertex.joints_low & 0xFFFFu, vertex.joints_low >> 16, vertex.joints_high & 0xFFFFu, vertex.joints_high >> 16) + first_joint;
    vec4 weight = vec4(vertex.weights & 0xFFu, (vertex.weights >> 8) & 0xFFu, (vertex.weights >> 16) & 0xFFu, vertex.weights >> 24) / 255.0;

    mat4 skin = joints.m[joint.x] * weight.x + joints.m[joint.y] * weight.y + joints.m[joint.z] * weight.z + joints.m[joint.w] * weight.w;
    skinned.p[first_out + i] = vec4((skin * vec4(vertex.position, 1.0)).xyz, 1.0);

}
