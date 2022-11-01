#version 450

layout (location = 0) in vec4 a_sphere;
layout (location = 1) in vec4 a_color;

layout (location = 0) out vec2 v_uv;
layout (location = 1) out vec4 v_color;
layout (location = 2) out flat vec4 v_sphere;
layout (location = 3) out vec3 v_pos;
layout (location = 4) out vec3 v_cam;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(set = 0, binding = 0) uniform Camera {
    mat4 projection;
    mat4 view;
} camera;

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
    v_sphere = a_sphere;
    v_color = a_color;
    gl_Position = camera.view * vec4(v_sphere.xyz, 1.0f);
    gl_Position.xy += (v_uv - 0.5) * v_sphere.w*2;

    mat4 inv = inverse(camera.view);
    vec4 cpos = gl_Position;
    v_pos = (inv * cpos).xyz;
    cpos.z -= 10;
    v_cam = (inv * cpos).xyz;

    gl_Position = camera.projection * gl_Position;

}

