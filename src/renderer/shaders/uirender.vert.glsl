#version 450

layout (location = 0) in vec2 a_position;
layout (location = 1) in vec2 a_uv;
layout (location = 2) in vec4 a_color;

layout (location = 0) out vec4 v_color;
layout (location = 1) out vec2 v_uv;

layout(push_constant) uniform uPushConstant{
    vec2 uScale;
    vec2 uTranslate;
} pc;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    gl_Position = vec4(a_position*pc.uScale+pc.uTranslate, 0, 1);
    v_color = a_color;
    v_uv = a_uv;

    gl_Position.y = -gl_Position.y;
}
