#version 450

layout(location = 0) in flat vec4 frag_color;
layout(location = 1) in flat vec3 frag_normal;

layout(location = 0) out vec4 outColor;

void main() {
    // Phong lighting with fixed sun direction
    vec3 sun_dir = normalize(vec3(1, -1, 1));
    vec3 normal = normalize(frag_normal);

    // Ambient + diffuse
    float ambient = 0.3;
    float diffuse = max(0.0, -dot(normal, sun_dir)) * 0.7;  // sun comes from up-left-forward
    float light = ambient + diffuse;

    // Apply lighting to color
    vec3 lit_color = frag_color.rgb * light;

    outColor = vec4(lit_color, frag_color.a);
}
