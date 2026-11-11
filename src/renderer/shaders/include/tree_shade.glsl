// The shade of the plants without rays (renderer/tree_shade.h) : shadows of their crowns on a map of the
// world seen from above. After view.glsl

layout(set = 0, binding = 1) uniform sampler2D u_tree_shade;

float shadeHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float shadeNoise(vec2 p) {
    vec2 cell = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(shadeHash(cell), shadeHash(cell + vec2(1, 0)), f.x),
               mix(shadeHash(cell + vec2(0, 1)), shadeHash(cell + vec2(1, 1)), f.x), f.y);
}

// Where on the ground the light puts the shadow of something `above` it. A light on the horizon would
// send it to infinity : no lower than TreeShade::min_height
vec2 shadeOffset(float above) {
    return sun_direction.xz / max(sun_direction.y, 0.2) * above;
}

// How much of the sun the plants hide from a point `above` its ground : 0 in the open, tree_shade.x under
// thick crowns. The map only knows how thick the crowns are : a noise lets flecks of sun through them
float treeShade(vec3 position, float above) {
    if(tree_shade.x <= 0.0) return 0.0;
    float far = length(position - viewpos.xyz);
    if(far >= tree_shade.z) return 0.0;
    vec2 ground = position.xz - shadeOffset(above);
    float depth = textureLod(u_tree_shade, ground * tree_shade.y, 0.0).r * 4.0;
    float flecks = mix(0.6 * shadeNoise(ground * 0.7) + 0.4 * shadeNoise(ground * 2.1 + 17.0), 0.5, smoothstep(25.0, 80.0, far));
    float shade = 1.0 - exp(-depth * (0.2 + 1.6 * flecks));
    return tree_shade.x * shade * (1.0 - smoothstep(0.75 * tree_shade.z, tree_shade.z, far));
}
