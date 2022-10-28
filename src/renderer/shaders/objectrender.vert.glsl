#version 450

layout (location = 0) out vec2 v_uv;
layout (location = 1) out vec4 v_color;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(set = 0, binding = 0) uniform Camera {
    mat4 projection;
    mat4 view;
};

struct Object {
    vec4 box;
    vec4 color;
};

const int MAX_OBJECTS = 5000;

layout(std430, set = 1, binding = 0) readonly buffer Objects {
    Object objects[MAX_OBJECTS];
};

const vec2 vertices[6] = vec2[] (
    vec2(0,0),
    vec2(1,0),
    vec2(0,1),
    vec2(0,1),
    vec2(1,0),
    vec2(1,1)
);

void main() {
    v_uv = vertices[gl_VertexIndex];
    vec3 pos = objects[gl_InstanceIndex].box.xyz;
    gl_Position = view * vec4(pos, 1.0f);
    gl_Position.xy += (v_uv - 0.5) * objects[gl_InstanceIndex].box.w / gl_Position.w;
    gl_Position = projection * gl_Position;
    v_color = objects[gl_InstanceIndex].color;
}

