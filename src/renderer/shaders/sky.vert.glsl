#version 450
#extension GL_GOOGLE_include_directive : require

// One triangle over the screen, at the far plane

#include "include/view.glsl"

layout(location = 0) out vec3 v_direction;

void main() {
    vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    gl_Position = vec4(corner, 1.0, 1.0);
    vec4 near = inv_viewproj * vec4(corner, 0.0, 1.0);
    vec4 far = inv_viewproj * vec4(corner, 1.0, 1.0);
    v_direction = far.xyz / far.w - near.xyz / near.w;
}
