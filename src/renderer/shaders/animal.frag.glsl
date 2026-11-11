#version 460
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in float v_above;

// The view, the lights, the shade of the plants (treeShade), outColor
#include "include/scene.glsl"

layout(set = 1, binding = 0) uniform sampler2D u_color;     // rgb, a : opacity when the material is alpha tested
layout(set = 1, binding = 1) uniform sampler2D u_normal;    // tangent space

layout(push_constant) uniform Animal {
    mat4 model;
    vec4 tint;      // w : height of the animal
    vec4 params;    // x : alpha cutoff, y : normal map, z : first joint in the buffer
};

// As the vegetation
const float exposure = 1.43;

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

    // Fur takes the light a little past the terminator, and darkens under the belly
    vec3 sun_color = light_color.rgb * exposure;
    float sun = clamp((n_dot_l + 0.2) / 1.2, 0.0, 1.0);
    float under = mix(0.7, 1.0, smoothstep(-1.0, 0.2, normal.y));
    vec3 ambient = ambientLight(v_position, normal, shade) * under + pointLights(v_position, normal, 0.2);
    vec3 unlit = base.rgb * ambient * exposure;
    vec3 lit = base.rgb * sun_color * sun;

    // Sheen of the coat against the light
    float rim = pow(1.0 - max(dot(normal, view), 0.0), 4.0);
    lit += base.rgb * sun_color * rim * 0.25 * max(n_dot_l + 0.3, 0.0);

    writeLit(unlit, lit, shade, v_position, 1.0);

}
