#version 450

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in float v_above;

layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos; // w : time
    vec4 tree_shade; // x : strength, y : 1 / width of the map in metres, z : distance where it ends
};

// Shadows of the crowns of the plants on the ground, seen from above (renderer/tree_shade.h)
layout(set = 0, binding = 1) uniform sampler2D u_tree_shade;

layout(set = 1, binding = 0) uniform sampler2D u_color;     // rgb, a : opacity when the material is alpha tested
layout(set = 1, binding = 1) uniform sampler2D u_normal;    // tangent space

layout(push_constant) uniform Animal {
    mat4 model;
    vec4 tint;      // w : height of the animal
    vec4 params;    // x : alpha cutoff, y : normal map, z : first joint in the buffer
};

// As the vegetation
const vec3 sun_direction = normalize(vec3(0.5, 1.0, 0.5));
const vec3 sun_color = vec3(1.0, 0.96, 0.88);
const vec3 sky_color = vec3(0.45, 0.6, 0.8);
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

// How much of the sun the plants hide from a point `above` its ground (vegetation.frag.glsl)
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
    if(params.x > 0.0 && base.a < params.x) discard;
    base.rgb *= tint.rgb;

    vec3 normal = normalize(v_normal) * (gl_FrontFacing ? 1.0 : -1.0);
    if(params.y > 0.5) {
        // The library has no tangents : the frame comes from the derivatives of the position and of the uv
        vec3 dp1 = dFdx(v_position), dp2 = dFdy(v_position);
        vec2 duv1 = dFdx(v_uv), duv2 = dFdy(v_uv);
        vec3 dp2perp = cross(dp2, normal), dp1perp = cross(normal, dp1);
        vec3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
        vec3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
        float scale = max(dot(tangent, tangent), dot(bitangent, bitangent));
        if(scale > 1e-20) {
            float inv = inversesqrt(scale);
            vec3 map = texture(u_normal, v_uv).xyz * 2.0 - 1.0;
            // Unity's maps have v down : the green goes the other way
            normal = normalize(tangent * inv * map.x - bitangent * inv * map.y + normal * max(map.z, 0.05));
        }
    }

    vec3 view = normalize(viewpos.xyz - v_position);
    float n_dot_l = dot(normal, sun_direction);

    float shade = treeShade(v_position, max(v_above, 0.0));
    float sunlit = 1.0 - shade;

    // Fur takes the light a little past the terminator, and darkens under the belly
    float sun = clamp((n_dot_l + 0.2) / 1.2, 0.0, 1.0) * sunlit;
    float under = mix(0.7, 1.0, smoothstep(-1.0, 0.2, normal.y));
    vec3 ambient = mix(ground_light, sky_light, normal.y * 0.5 + 0.5) * under * (1.0 - 0.4 * shade);
    vec3 lit = base.rgb * (sun_color * sun + ambient);

    // Sheen of the coat against the light
    float rim = pow(1.0 - max(dot(normal, view), 0.0), 4.0);
    lit += base.rgb * sun_color * rim * 0.25 * max(n_dot_l + 0.3, 0.0) * sunlit;

    float fog = 1.0 - exp(-length(v_position - viewpos.xyz) * 0.0006);
    outColor = vec4(mix(lit, sky_color, fog), 1.0);

}
