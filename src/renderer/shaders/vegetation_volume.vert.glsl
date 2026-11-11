#version 450

// A plant far away, drawn from the baked volume of its species (vegetation_volume.rt.frag.glsl) : the box
// of its bounds, twelve triangles without a vertex buffer

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos;
};

layout(push_constant) uniform Box {
    mat4 model;
    vec4 low;       // xyz : corner of the volume in the space of the plant, w : index of its textures
    vec4 size;      // xyz : of the volume in metres, w : a texel in metres
};

layout(location = 0) out vec3 v_position;       // in the world
layout(location = 1) out vec3 v_local;          // in the box, 0..1
layout(location = 2) flat out vec3 v_eye;       // the eye in the box

void main() {

    // Faces of the unit cube, front faces counter clockwise on screen with the engine's projection
    const int corners[36] = int[](
        0, 2, 1, 1, 2, 3,   4, 5, 6, 5, 7, 6,
        0, 1, 4, 1, 5, 4,   2, 6, 3, 3, 6, 7,
        0, 4, 2, 2, 4, 6,   1, 3, 5, 3, 7, 5);
    int corner = corners[gl_VertexIndex];
    vec3 local = vec3(corner & 1, (corner >> 1) & 1, corner >> 2);

    vec4 world = model * vec4(low.xyz + local * size.xyz, 1.0);
    v_position = world.xyz;
    v_local = local;
    v_eye = ((inverse(model) * vec4(viewpos.xyz, 1.0)).xyz - low.xyz) / size.xyz;
    gl_Position = viewproj * world;

}
