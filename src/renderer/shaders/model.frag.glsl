#version 450

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;

layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos; // w : time
};

// Base color texture of the material, white when untextured
layout(set = 1, binding = 0) uniform sampler2D u_base_color;

layout(push_constant) uniform Model {
    mat4 model;
    vec4 color; // material base color factor * instance tint
};

const vec3 sun_direction = normalize(vec3(0.5, 1.0, 0.5));
const vec3 sky_color = vec3(0.45, 0.6, 0.8);

void main() {

    vec4 base = texture(u_base_color, v_uv) * color;

    vec3 normal = normalize(v_normal);
    float light = max(dot(normal, sun_direction), 0.0);
    vec3 lit = base.rgb * (light * 0.7 + 0.3);

    float fog = 1.0 - exp(-length(v_position - viewpos.xyz) * 0.0006);
    outColor = vec4(mix(lit, sky_color, fog), base.a);

}
