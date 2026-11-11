#version 460
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;

// The view, the lights, outColor
#include "include/scene.glsl"

// Base color texture of the material, white when untextured
layout(set = 1, binding = 0) uniform sampler2D u_base_color;

layout(push_constant) uniform Model {
    mat4 model;
    vec4 color; // material base color factor * instance tint
};

void main() {

    vec4 base = texture(u_base_color, v_uv) * color;

    vec3 normal = normalize(v_normal);
    float light = max(dot(normal, sun_direction), 0.0);
    vec3 sunlit = base.rgb * light_color.rgb * light;
    vec3 unlit = base.rgb * (ambientLight(v_position, normal, 0.0) + pointLights(v_position, normal, 0.0));

    writeLit(unlit, sunlit, 0.0, v_position, base.a);

}
