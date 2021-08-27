#version 450

layout (location = 0) out vec2 v_uv;
layout (location = 1) out vec4 v_color;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(set = 0, binding = 0) uniform Camera {
    vec2 position;
    vec2 size;
};

struct Object {
    vec4 box;
    vec4 color;
};

const int MAX_OBJECTS = 5000;

layout(std140, set = 1, binding = 0) uniform Objects {
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
    vec2 pos = objects[gl_InstanceIndex].box.xy + v_uv * objects[gl_InstanceIndex].box.zw - position;
    gl_Position = vec4(pos / size * 2.f - 1.f, 0.5f, 1.0f);
    v_color = objects[gl_InstanceIndex].color;
}

