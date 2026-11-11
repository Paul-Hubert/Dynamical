#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

// How much of the sun (or of the moon) each pixel sees this frame (renderer/lighting/sun_visibility.h) :
// rays from the point of the world under the pixel, found from the depth, to points of the disc of the
// light, within the traced range of the target. One or two rays : noise, which sun_resolve.frag.glsl
// blends over the frames

#include "include/view.glsl"
#include "include/rays.glsl"
#include "include/probes.glsl"

layout(location = 0) in vec2 v_uv;

// rgb : the light of the sun that reaches the pixel, a : distance of the pixel
layout(location = 0) out vec4 outVisibility;

layout(set = 1, binding = 0) uniform sampler2D u_depth;
layout(set = 1, binding = 1) uniform sampler2D u_color;        // a : where the ray to the sun starts
layout(set = 1, binding = 2) uniform sampler2D u_sun;
layout(set = 1, binding = 3) uniform sampler2D u_unused;

layout(push_constant) uniform Args {
    vec2 texel;
    float history;
};

uint hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float random(inout uint seed) {
    seed = hash(seed);
    return float(seed) * (1.0 / 4294967296.0);
}

void main() {

    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(u_depth, pixel, 0).r;
    float range = view_lighting.x;
    int rays = int(view_lighting.y);

    // The sky, and a light that is not there
    if(depth >= 1.0 || dot(light_color.rgb, vec3(1.0)) <= 0.0) {
        outVisibility = vec4(1.0, 1.0, 1.0, 1e4);
        return;
    }

    vec4 world = inv_viewproj * vec4(v_uv * 2.0 - 1.0, depth, 1.0);
    vec3 position = world.xyz / world.w;
    vec3 to_eye = viewpos.xyz - position;
    float far = length(to_eye);
    // Beyond the traced range, and more and more over its last fifth : what the probes around saw of the
    // sun, soft
    float beyond = rays > 0 ? smoothstep(0.8 * range, range, far) : 1.0;
    vec3 from_probes = beyond > 0.0 ? probeDirect(position + vec3(0.0, 1.0, 0.0)).rgb : vec3(1.0);
    if(beyond >= 1.0) {
        outVisibility = vec4(from_probes, max(far, range));
        return;
    }

    // Off the surface : towards the light (a leaf is not its own shadow, from either side) and a little
    // towards the eye, further where the depth is coarse, and up by what the pass that drew the pixel
    // asked for (the grass, whose hits may be under the ground; the facets of the ground)
    float lift = (1.0 - texelFetch(u_color, pixel, 0).a) * 2.0;
    float bias = 0.02 + 0.0008 * far;
    vec3 origin = position + sun_direction * bias + to_eye / far * (0.3 * bias) + vec3(0.0, lift, 0.0);

    // Points of the disc of the light, others every frame
    vec3 side = normalize(cross(sun_direction, abs(sun_direction.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 over = cross(sun_direction, side);
    uint seed = uint(pixel.x) * 1973u + uint(pixel.y) * 9277u + uint(light_frame.x) * 26699u;

    vec3 visible = vec3(0.0);
    for(int i = 0; i < rays; i++) {
        float radius = light_direction.w * sqrt(random(seed));
        float angle = 6.2831853 * random(seed);
        vec3 direction = normalize(sun_direction + (side * cos(angle) + over * sin(angle)) * radius);
        visible += traceShadow(origin, direction, 4000.0, ray_all);
    }

    outVisibility = vec4(mix(visible / float(rays), from_probes, beyond), far);

}
