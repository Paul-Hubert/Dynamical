#version 450
#extension GL_GOOGLE_include_directive : require

// With ray tracing : what the scene passes drew apart comes together (renderer/lighting/sun_visibility.h).
// colour + sun x visibility, into the image the target shows. The visibility is smoothed over the pixels
// around that are on the same surface : what is left of the grain of its rays

#include "include/view.glsl"

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D u_depth;
layout(set = 1, binding = 1) uniform sampler2D u_color;        // a : where the ray to the sun starts
layout(set = 1, binding = 2) uniform sampler2D u_sun;
layout(set = 1, binding = 3) uniform sampler2D u_visibility;   // rgb : the sun seen from the pixel, a : its distance

layout(push_constant) uniform Args {
    vec2 texel;
    float history;
};

void main() {

    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 color = texelFetch(u_color, pixel, 0).rgb;
    vec3 sun = texelFetch(u_sun, pixel, 0).rgb;
    vec4 centre = texelFetch(u_visibility, pixel, 0);

    vec3 visible = centre.rgb;
    if(centre.a < view_lighting.x) {
        vec3 sum = vec3(0.0);
        float weight = 0.0;
        float tolerance = 0.04 * centre.a + 0.1;
        for(int y = -1; y <= 1; y++) {
            for(int x = -1; x <= 1; x++) {
                vec4 near = texelFetch(u_visibility, pixel + ivec2(x, y), 0);
                float w = (abs(near.a - centre.a) < tolerance ? 1.0 : 0.0) * (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
                sum += near.rgb * w;
                weight += w;
            }
        }
        visible = sum / weight;
    }

    outColor = vec4(color + sun * visible, 1.0);

}
