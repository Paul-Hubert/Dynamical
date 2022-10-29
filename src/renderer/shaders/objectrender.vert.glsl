#version 450

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
    ivec2 screen_size;
} camera;

struct Object {
    vec4 sphere;
    vec4 color;
};

const int MAX_OBJECTS = 20000;

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
    v_sphere = objects[gl_InstanceIndex].sphere;
    v_color = objects[gl_InstanceIndex].color;
    v_sphere.z *= -1;
    gl_Position = camera.view * vec4(v_sphere.xyz, 1.0f);
    gl_Position.xy += (v_uv - 0.5) * v_sphere.w*2;

    mat4 inv = inverse(camera.view);
    v_pos = (inv * gl_Position).xyz;
    v_cam = (inv * vec4(gl_Position.xy, 1, 1)).xyz;

    gl_Position = camera.projection * gl_Position;

    gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;

}

