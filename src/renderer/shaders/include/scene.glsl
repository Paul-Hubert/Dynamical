// What every scene fragment shader shares : the view and the lights, the shade of the plants, the point
// lights, and how the lit colour leaves the shader (docs/lighting.md)

#include "view.glsl"

// Shadows of the crowns of the plants on the ground, seen from above (renderer/tree_shade.h)
layout(set = 0, binding = 1) uniform sampler2D u_tree_shade;

layout(location = 0) out vec4 outColor;

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
    float depth = texture(u_tree_shade, ground * tree_shade.y).r * 4.0;
    float flecks = mix(0.6 * shadeNoise(ground * 0.7) + 0.4 * shadeNoise(ground * 2.1 + 17.0), 0.5, smoothstep(25.0, 80.0, far));
    float shade = 1.0 - exp(-depth * (0.2 + 1.6 * flecks));
    return tree_shade.x * shade * (1.0 - smoothstep(0.75 * tree_shade.z, tree_shade.z, far));
}

// Light of the sky and of the ground on a surface, without the probes : less under the plants
vec3 ambientLight(vec3 position, vec3 normal, float shade) {
    return mix(ambient_ground.rgb, ambient_sky.rgb, normal.y * 0.5 + 0.5) * (1.0 - 0.35 * shade);
}

// Light of the torches on a surface. wrap : how far past the edge of the light a soft thing (fur, leaves,
// grass) is still lit, 0 for a hard one
vec3 pointLights(vec3 position, vec3 normal, float wrap) {
    vec3 sum = vec3(0.0);
    int count = int(ambient_ground.w);
    for(int i = 0; i < count; i++) {
        vec3 to = point_lights[i].position.xyz - position;
        float radius = point_lights[i].position.w;
        float d2 = dot(to, to);
        if(d2 >= radius * radius) continue;
        float d = sqrt(d2);
        float window = 1.0 - d2 * d2 / (radius * radius * radius * radius);
        float facing = max((dot(normal, to) / max(d, 1e-4) + wrap) / (1.0 + wrap), 0.0);
        sum += point_lights[i].color.rgb * (facing * window * window / (d2 + 0.25));
    }
    return sum;
}

// unlit : what the sky, the bounces and the torches give. sunlit : what the sun (or the moon) adds where
// nothing hides it. shade : how much of it the plants hide (treeShade)
void writeLit(vec3 unlit, vec3 sunlit, float shade, vec3 position, float alpha) {
    vec3 view = position - viewpos.xyz;
    float far = length(view);
    float fog = fogAmount(far);
    outColor = vec4(mix(unlit + sunlit * (1.0 - shade), fogColor(view / max(far, 1e-4)), fog), alpha);
}

// What lights itself : the sky
void writeUnlit(vec3 color) {
    outColor = vec4(color, 1.0);
}
