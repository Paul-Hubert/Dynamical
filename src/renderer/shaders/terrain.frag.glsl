#version 450

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;

layout(location = 0) out vec4 outColor;

// Layer 0 : grass, layer 1 : rock
layout(set = 1, binding = 0) uniform sampler2DArray u_color;

// Only pushed back, under the grass : keeps the early depth test
layout(depth_greater) out float gl_FragDepth;

layout(push_constant) uniform Args {
    float under_grass;  // height of the grass that the grass pass draws over the flat ground, 0 without it
};

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

    // Under the grass (same slopes as grass.vert.glsl) : in its shade, and behind it. The grass pass
    // only knows the ground as a plane under each pixel, its hits can end up a little under the terrain :
    // the terrain steps back by the height of the grass and lets them through
    gl_FragDepth = gl_FragCoord.z;
    if(under_grass > 0.0) {
        float covered = smoothstep(0.76, 0.88, normal.y);
        outColor.rgb *= 1.0 - 0.8 * covered;
        vec4 clip = viewproj * vec4(v_position + normalize(v_position - viewpos.xyz) * (under_grass * covered), 1.0);
        gl_FragDepth = clamp(max(clip.z / clip.w, gl_FragCoord.z), 0.0, 1.0);
    }

    float fog = 1.0 - exp(-length(v_position - viewpos.xyz) * 0.0006);
    outColor.rgb = mix(outColor.rgb, sky_color, fog);
    outColor.a = 1.0;

}
