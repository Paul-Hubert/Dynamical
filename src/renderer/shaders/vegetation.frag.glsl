#version 460
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_uv;
layout(location = 4) in vec2 v_shade;
layout(location = 5) in float v_above;

// The view, the lights, the shade of the plants (treeShade), outColor
#include "include/scene.glsl"

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

// SpeedTree's maps are dark : meant for more light than the ground gets
const float exposure = 1.43;

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

    // Without shadows inside the crown the baked occlusion also dims the sun there
    vec3 sun_color = light_color.rgb * exposure;
    float sun = max(n_dot_l, 0.0) * mix(1.0, occlusion, 0.5);
    vec3 ambient = ambientLight(v_position, normal, shade) * occlusion + pointLights(v_position, normal, 0.3);
    vec3 unlit = base.rgb * ambient * exposure;
    vec3 lit = base.rgb * sun_color * sun;

    // Sun seen through the leaves, stronger looking toward it
    if(params.w > 0.5) {
        vec3 through = texture(u_subsurface, v_uv).rgb;
        float back = max(-n_dot_l, 0.0);
        float toward = pow(max(dot(view, -sun_direction), 0.0), 3.0);
        lit += through * sun_color * back * (0.4 + 0.6 * toward) * mix(1.0, occlusion, 0.5);
    }

    // Sheen of smooth leaves and bark
    vec3 halfway = normalize(sun_direction + view);
    float specular = pow(max(dot(normal, halfway), 0.0), mix(8.0, 96.0, extra.r)) * extra.r * step(0.0, n_dot_l);
    lit += sun_color * specular * 0.25 * occlusion;

    writeLit(unlit, lit, shade, v_position, 1.0);

}
