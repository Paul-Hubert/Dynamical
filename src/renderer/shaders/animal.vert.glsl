#version 450

// Skinned meshes of the animal library (tools/animal_import.py) : four joints a vertex

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in uvec4 a_joints;
layout(location = 4) in vec4 a_weights;

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out float v_above;     // height above the feet of the animal

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos; // w : time
};

// Skinning matrices of every animal drawn this frame : bind pose to pose, in model space
layout(std430, set = 2, binding = 0) readonly buffer Joints {
    mat4 joints[];
};

layout(push_constant) uniform Animal {
    mat4 model;
    vec4 tint;      // w : height of the animal
    vec4 params;    // x : alpha cutoff, y : normal map, z : first joint in the buffer
};

void main() {

    uint first = uint(params.z);
    mat4 skin = joints[first + a_joints.x] * a_weights.x
              + joints[first + a_joints.y] * a_weights.y
              + joints[first + a_joints.z] * a_weights.z
              + joints[first + a_joints.w] * a_weights.w;

    vec4 world = model * (skin * vec4(a_position, 1.0));

    v_position = world.xyz;
    // Joints and animals are scaled uniformly
    v_normal = normalize(mat3(model) * (mat3(skin) * a_normal.xyz));
    v_uv = a_uv;
    v_above = world.y - model[3].y;

    gl_Position = viewproj * world;

}
