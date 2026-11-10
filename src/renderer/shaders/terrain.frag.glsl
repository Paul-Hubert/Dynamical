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
    vec4 tree_shade; // x : strength, y : 1 / width of the map in metres, z : distance where it ends
};

// Shadows of the crowns of the plants on the ground, seen from above (renderer/tree_shade.h)
layout(set = 0, binding = 1) uniform sampler2D u_tree_shade;

const vec3 sun_direction = normalize(vec3(0.5, 1.0, 0.5));
const vec3 sky_color = vec3(0.45, 0.6, 0.8);

float shadeHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float shadeNoise(vec2 p) {
    vec2 cell = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(shadeHash(cell), shadeHash(cell + vec2(1, 0)), f.x),
               mix(shadeHash(cell + vec2(0, 1)), shadeHash(cell + vec2(1, 1)), f.x), f.y);
}

// How much of the sun the plants hide from a point `above` its ground : 0 in the open, tree_shade.x under
// thick crowns. The map only knows how thick the crowns are : a noise lets flecks of sun through them
float treeShade(vec3 position, float above) {
    if(tree_shade.x <= 0.0) return 0.0;
    float far = length(position - viewpos.xyz);
    if(far >= tree_shade.z) return 0.0;
    vec2 ground = position.xz - sun_direction.xz / sun_direction.y * above;
    float depth = texture(u_tree_shade, ground * tree_shade.y).r * 4.0;
    float flecks = mix(0.6 * shadeNoise(ground * 0.7) + 0.4 * shadeNoise(ground * 2.1 + 17.0), 0.5, smoothstep(25.0, 80.0, far));
    float shade = 1.0 - exp(-depth * (0.2 + 1.6 * flecks));
    return tree_shade.x * shade * (1.0 - smoothstep(0.75 * tree_shade.z, tree_shade.z, far));
}

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

    // Under the plants : little sun, and less of the sky
    float shade = treeShade(v_position, 0.0);
    float light = max(dot(normal, sun_direction), 0.0) * (1.0 - shade);
    outColor.rgb = outColor.rgb * (light * 0.7 + 0.3 * (1.0 - 0.35 * shade));

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
