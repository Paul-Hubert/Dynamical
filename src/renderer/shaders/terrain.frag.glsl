#version 450

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;

layout(location = 0) out vec4 outColor;

// Layer 0 : grass, layer 1 : rock
layout(set = 1, binding = 0) uniform sampler2DArray u_color;

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos; // w : time
};

const vec3 sun_direction = normalize(vec3(0.5, 1.0, 0.5));
const vec3 sky_color = vec3(0.45, 0.6, 0.8);

void main() {

    vec3 normal = normalize(v_normal);

    // Triplanar rock on the sides, grass on top
    vec4 rock_color = max(mix(
            texture(u_color, vec3(v_position.yx / 3., 1)),
            texture(u_color, vec3(v_position.yz / 3., 1)),
            (normal.x * normal.x) / (normal.x * normal.x + normal.z * normal.z + 0.0001)
        ), 0);

    outColor = mix(
        rock_color,
        texture(u_color, vec3(v_position.xz / 3., 0)),
        clamp(normal.y * normal.y * normal.y, 0, 1)
    );

    float light = max(dot(normal, sun_direction), 0.0);
    outColor.rgb = outColor.rgb * (light * 0.7 + 0.3);

    float fog = 1.0 - exp(-length(v_position - viewpos.xyz) * 0.0006);
    outColor.rgb = mix(outColor.rgb, sky_color, fog);
    outColor.a = 1.0;

}
