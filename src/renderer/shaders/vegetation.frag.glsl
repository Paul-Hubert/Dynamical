#version 450

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_uv;
layout(location = 4) in vec2 v_shade;
layout(location = 5) in float v_above;

layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos; // w : time
    vec4 tree_shade; // x : strength, y : 1 / width of the map in metres, z : distance where it ends
};

// Shadows of the crowns of the plants on the ground, seen from above (renderer/tree_shade.h)
layout(set = 0, binding = 1) uniform sampler2D u_tree_shade;

// SpeedTree 8 maps
layout(set = 1, binding = 0) uniform sampler2D u_color;         // rgb, a : opacity
layout(set = 1, binding = 1) uniform sampler2D u_normal;        // tangent space, y up
layout(set = 1, binding = 2) uniform sampler2D u_extra;         // r : smoothness, g : metallic, b : ambient occlusion
layout(set = 1, binding = 3) uniform sampler2D u_subsurface;    // rgb : light going through the leaf

layout(push_constant) uniform Plant {
    mat4 model;
    vec4 tint;  // w : height of the plant
    vec4 params; // x : alpha cutoff, y : billboard cards, z : normal map, w : subsurface
};

const vec3 sun_direction = normalize(vec3(0.5, 1.0, 0.5));
const vec3 sun_color = vec3(1.0, 0.96, 0.88);
const vec3 sky_color = vec3(0.45, 0.6, 0.8);
// Ambient light from above and bounced from the ground
const vec3 sky_light = vec3(0.5, 0.56, 0.66);
const vec3 ground_light = vec3(0.3, 0.3, 0.24);

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

    vec4 base = texture(u_color, v_uv);
    if(base.a * v_shade.y < params.x) discard;
    base.rgb *= tint.rgb;

    // Leaves are lit from both sides
    float side = gl_FrontFacing ? 1.0 : -1.0;
    vec3 normal = normalize(v_normal) * side;
    if(params.z > 0.5) {
        vec3 tangent = v_tangent.xyz - normal * dot(normal, v_tangent.xyz);
        if(dot(tangent, tangent) > 1e-6) {
            tangent = normalize(tangent);
            vec3 bitangent = cross(normalize(v_normal), tangent) * v_tangent.w;
            // Only x and y are stored, as Unity reads these maps : the blue of the billboard atlases is not a z
            vec3 map;
            map.xy = texture(u_normal, v_uv).xy * 2.0 - 1.0;
            map.z = sqrt(max(1.0 - dot(map.xy, map.xy), 0.0));
            // Billboard atlases : as Unity's SpeedTree8 shader, which leans their normals away from the card
            if(params.y > 0.5) map.z *= 0.5;
            normal = normalize(tangent * map.x + bitangent * map.y + normal * max(map.z, 0.05));
        }
    }

    vec3 extra = texture(u_extra, v_uv).rgb;
    float occlusion = mix(1.0, v_shade.x * extra.b, 0.85);

    vec3 view = normalize(viewpos.xyz - v_position);
    float n_dot_l = dot(normal, sun_direction);

    // In the shade of the plants around. The map has the shadow of this plant too, which is not on its own
    // crown : a tall plant leaves it as it goes up (a small one has next to no shadow of its own)
    float own = clamp((tint.w - 1.5) / 4.0, 0.0, 1.0) * smoothstep(0.3, 0.75, v_above / max(tint.w, 0.01));
    float shade = treeShade(v_position, max(v_above, 0.0)) * (1.0 - own);
    float sunlit = 1.0 - shade;

    // Without shadows inside the crown the baked occlusion also dims the sun there
    float sun = max(n_dot_l, 0.0) * mix(1.0, occlusion, 0.5) * sunlit;
    vec3 ambient = mix(ground_light, sky_light, normal.y * 0.5 + 0.5) * occlusion * (1.0 - 0.4 * shade);
    vec3 lit = base.rgb * (sun_color * sun + ambient);

    // Sun seen through the leaves, stronger looking toward it
    if(params.w > 0.5) {
        vec3 through = texture(u_subsurface, v_uv).rgb;
        float back = max(-n_dot_l, 0.0);
        float toward = pow(max(dot(view, -sun_direction), 0.0), 3.0);
        lit += through * sun_color * back * (0.4 + 0.6 * toward) * mix(1.0, occlusion, 0.5) * sunlit;
    }

    // Sheen of smooth leaves and bark
    vec3 halfway = normalize(sun_direction + view);
    float specular = pow(max(dot(normal, halfway), 0.0), mix(8.0, 96.0, extra.r)) * extra.r * step(0.0, n_dot_l);
    lit += sun_color * specular * 0.25 * occlusion * sunlit;

    float fog = 1.0 - exp(-length(v_position - viewpos.xyz) * 0.0006);
    outColor = vec4(mix(lit, sky_color, fog), 1.0);

}
