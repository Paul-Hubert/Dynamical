#version 450

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos; // w : time
};

layout(push_constant) uniform Model {
    mat4 model;
    vec4 color;
};

void main() {

    vec4 world = model * vec4(a_pos, 1.0);
    v_position = world.xyz;
    v_normal = mat3(model) * a_normal;
    v_uv = a_uv;
    gl_Position = viewproj * world;

}
