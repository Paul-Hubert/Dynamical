// What every scene fragment shader shares : the view and the lights, the shade of the plants, the point
// lights, and how the lit colour leaves the shader (docs/lighting.md)

// With ray tracing : the rays of the torches and the probes. Declared here for every scene shader
#ifdef RAY_TRACING
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require
#endif

#include "view.glsl"
#include "tree_shade.glsl"

#ifdef RAY_TRACING
#include "rays.glsl"
#include "probes.glsl"
#endif

// Without ray tracing : the lit colour, into the image the target shows.
// With it (RAY_TRACING, renderer/lighting/sun_visibility.h) : everything but the light of the sun, and that
// light apart, to be multiplied by how much of the sun the pixel sees. The alpha of the colour is then
// where the ray to the sun starts (liftAlpha)
layout(location = 0) out vec4 outColor;
#ifdef RAY_TRACING
layout(location = 1) out vec4 outSun;
#endif

// Light of the sky and of the ground on a surface, without the probes : less under the plants
vec3 ambientLight(vec3 position, vec3 normal, float shade) {
    vec3 open = mix(ambient_ground.rgb, ambient_sky.rgb, normal.y * 0.5 + 0.5) * (1.0 - 0.35 * shade);
#ifdef RAY_TRACING
    // The probes around : the sky they see, what bounces
    return probeLight(position, normal, open);
#else
    return open;
#endif
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
        vec3 light = point_lights[i].color.rgb * (facing * window * window / (d2 + 0.25));
#ifdef RAY_TRACING
        // The nearest torches of the frame cast shadows, hard ones : a small source
        if(float(i) < view_lighting.z && facing > 0.0) {
            vec3 towards = to / max(d, 1e-4);
            light *= traceShadow(position + (normal + towards) * 0.04, towards, max(d - 0.15, 0.0), ray_all);
        }
#endif
        sum += light;
    }
    return sum;
}

// unlit : what the sky, the bounces and the torches give. sunlit : what the sun (or the moon) adds where
// nothing hides it. shade : how much of it the plants hide (treeShade)
// alpha : of a blended material, 1 for the others, or liftAlpha() where the shadow ray must start over the pixel
void writeLit(vec3 unlit, vec3 sunlit, float shade, vec3 position, float alpha) {
    vec3 view = position - viewpos.xyz;
    float far = length(view);
    float fog = fogAmount(far);
    vec3 behind = fogColor(view / max(far, 1e-4));
#ifdef RAY_TRACING
    // What hides the sun is found afterwards : rays near the eye, the probes beyond
    outColor = vec4(mix(unlit, behind, fog), alpha);
    outSun = vec4(sunlit * (1.0 - fog), alpha);
#else
    outColor = vec4(mix(unlit + sunlit * (1.0 - shade), behind, fog), alpha);
#endif
}

// The alpha that makes the ray to the sun start `metres` over what the pixel shows (at most 2) : for what
// is drawn a little under the ground it stands on. 1 without ray tracing
float liftAlpha(float metres) {
#ifdef RAY_TRACING
    return 1.0 - clamp(metres * 0.5, 0.0, 1.0);
#else
    return 1.0;
#endif
}

// The ground is facets of a few metres under smooth normals : where a facet turns away from a low sun
// while its normals still face it, the facet next to it would hide the sun from all of it at once and the
// shadow of a round hill would end along the edges of its triangles. The ray starts higher there, by
// what clears a neighbour `facet` metres away
float terminatorLift(vec3 position_dx, vec3 position_dy, vec3 smooth_normal, float facet) {
    vec3 flat_normal = cross(position_dx, position_dy);
    float size = length(flat_normal);
    if(size <= 0.0 || dot(smooth_normal, sun_direction) <= 0.0) return 0.0;
    flat_normal *= sign(dot(flat_normal, smooth_normal)) / size;
    return clamp(1.4 * facet * (0.08 - dot(flat_normal, sun_direction)), 0.0, 2.0);
}

// What lights itself : the sky
void writeUnlit(vec3 color) {
    outColor = vec4(color, 1.0);
#ifdef RAY_TRACING
    outSun = vec4(0.0, 0.0, 0.0, 1.0);
#endif
}
