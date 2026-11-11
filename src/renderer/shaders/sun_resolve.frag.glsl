#version 450
#extension GL_GOOGLE_include_directive : require

// The sun's visibility over time (renderer/lighting/sun_visibility.h). What the rays of this frame found is
// noise : a pixel takes a tenth of it and the rest from what the same point of the world had the frame
// before. That point is found on the screen of the frame before from the depth alone (the world does not
// move); it is refused where something else was there, and held to what the pixels around say now, so that
// the shadow of what moves does not trail. A pixel without a past takes the mean of the rays around it

#include "include/view.glsl"

layout(location = 0) in vec2 v_uv;

// rgb : the light of the sun that reaches the pixel, a : distance of the pixel
layout(location = 0) out vec4 outVisibility;

layout(set = 1, binding = 0) uniform sampler2D u_depth;
layout(set = 1, binding = 1) uniform sampler2D u_rays;         // of this frame
layout(set = 1, binding = 2) uniform sampler2D u_sun;
layout(set = 1, binding = 3) uniform sampler2D u_history;      // the visibility of the frame before

layout(push_constant) uniform Args {
    vec2 texel;
    float history;      // frames the history holds
};

const float fresh = 0.1;    // part of the new rays in a pixel that has a past

void main() {

    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 now = texelFetch(u_rays, pixel, 0);
    float depth = texelFetch(u_depth, pixel, 0).r;

    // Nothing was traced : the sky, beyond the range
    if(depth >= 1.0 || now.a >= view_lighting.x) {
        outVisibility = now;
        return;
    }

    // What the rays around found, on the same surface : mean and spread
    vec3 sum = vec3(0.0), squares = vec3(0.0);
    float weight = 0.0;
    float tolerance = 0.04 * now.a + 0.1;
    for(int y = -2; y <= 2; y++) {
        for(int x = -2; x <= 2; x++) {
            vec4 near = texelFetch(u_rays, pixel + ivec2(x, y), 0);
            float same = abs(near.a - now.a) < tolerance ? 1.0 : 0.0;
            sum += near.rgb * same;
            squares += near.rgb * near.rgb * same;
            weight += same;
        }
    }
    vec3 mean = sum / max(weight, 1.0);
    vec3 spread = sqrt(max(squares / max(weight, 1.0) - mean * mean, vec3(0.0)));

    // Where the point was a frame before
    vec4 world = inv_viewproj * vec4(v_uv * 2.0 - 1.0, depth, 1.0);
    vec3 position = world.xyz / world.w;
    vec4 before = prev_viewproj * vec4(position, 1.0);
    vec2 at = before.xy / before.w * 0.5 + 0.5;

    bool known = history >= 1.0 && before.w > 0.0 && all(greaterThan(at, texel)) && all(lessThan(at, 1.0 - texel));
    vec4 past = vec4(0.0);
    if(known) {
        past = texture(u_history, at);
        float expected = distance(prev_viewpos.xyz, position);
        known = abs(past.a - expected) < 0.03 * expected + 0.08;
    }

    vec3 visible = mean;
    if(known) {
        // The past, no further from the mean of now than the rays around spread : a shadow that left is
        // forgotten at once, the grain of a penumbra is kept
        vec3 held = clamp(past.rgb, mean - 1.5 * spread - 0.02, mean + 1.5 * spread + 0.02);
        visible = mix(held, now.rgb, fresh);
    }

    outVisibility = vec4(visible, now.a);

}
