#version 460
#extension GL_GOOGLE_include_directive : require

// A plant far away, drawn from the baked volume of its species (renderer/lighting/plant_volumes.h) in
// place of its billboard : round from any side, from above too, and the same from wherever it is seen.
// The pixel is on the box of the plant; its view ray is marched through the density of the volume and
// stops where what it crossed passes a threshold drawn per pixel. There, the mean colour and normal of
// the plant are lit as a leaf is, through the rest of its own crown towards the sun

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_local;
layout(location = 2) flat in vec3 v_eye;

#include "include/scene.glsl"

// The plant is behind its box : keeps the early depth test
layout(depth_greater) out float gl_FragDepth;

layout(push_constant) uniform Box {
    mat4 model;
    vec4 low;       // xyz : corner of the volume in the space of the plant, w : index of its textures
    vec4 size;      // xyz : of the volume in metres, w : a texel in metres
};

// As the vegetation
const float exposure = 1.43;

float pixelHash(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {

    int volume = int(low.w);

    // The view ray in the box : v_eye + along * t, on the box at t = 1
    vec3 along = v_local - v_eye;
    vec3 inverse_along = 1.0 / along;
    vec3 a = -v_eye * inverse_along, b = (1.0 - v_eye) * inverse_along;
    vec3 high = max(a, b);
    float leave = min(high.x, min(high.y, high.z));
    // Both sides of the box are drawn : the far one has nothing behind it
    if(leave <= 1.0001) discard;

    float far = length(v_position - viewpos.xyz);
    float scale = length(model[1].xyz);
    // Texels of the volume along the ray for a t of 1, and a pixel in texels : the level that is read
    float texels = length(along * size.xyz) / size.w;
    vec3 view = (v_position - viewpos.xyz) / far;
    float pixel = far * max(length(dFdx(view)), length(dFdy(view))) / (size.w * scale);
    float level = clamp(log2(max(pixel, 1.0)), 0.0, 3.0);

    int steps = int(clamp(ceil((leave - 1.0) * texels / exp2(level)), 4.0, 32.0));
    float span = (leave - 1.0) / float(steps);
    float crossed = span * texels;

    // What the ray has met so far, against the threshold of the pixel
    float threshold = mix(0.2, 0.8, pixelHash(gl_FragCoord.xy));
    float start = pixelHash(gl_FragCoord.yx + 17.0);
    float clear = 1.0;
    float stop = -1.0;
    for(int i = 0; i < steps; i++) {
        float t = 1.0 + (float(i) + start) * span;
        float likely = min(textureLod(u_plant_density[nonuniformEXT(volume)], v_eye + along * t, level).a, 0.999);
        clear *= pow(1.0 - likely, crossed);
        if(1.0 - clear >= threshold) {
            stop = t;
            break;
        }
    }
    if(stop < 0.0) discard;

    vec3 at = v_eye + along * stop;
    vec3 position = viewpos.xyz + (v_position - viewpos.xyz) * stop;
    vec4 clip = viewproj * vec4(position, 1.0);
    gl_FragDepth = clamp(max(clip.z / clip.w, gl_FragCoord.z), 0.0, 1.0);

    vec4 colour = textureLod(u_plant_color[nonuniformEXT(volume)], at, level);
    vec4 baked = textureLod(u_plant_normal[nonuniformEXT(volume)], at, level);
    mat3 turn = mat3(model) / scale;
    vec3 normal = normalize(turn * octDecode(baked.rg));

    // The sun through the rest of the crown : a few steps towards it in the volume
    vec3 through = vec3(1.0);
    {
        vec3 to_sun = (transpose(turn) * sun_direction) / (scale * size.xyz);    // in the box, a metre of the world
        vec3 exits = (mix(vec3(0.0), vec3(1.0), greaterThan(to_sun, vec3(0.0))) - at) / to_sun;
        float out_at = min(exits.x, min(exits.y, exits.z));
        const int sun_steps = 6;
        float sun_span = out_at / float(sun_steps);
        float sun_texels = sun_span * length(to_sun * size.xyz) / size.w;
        for(int i = 0; i < sun_steps; i++) {
            vec3 stopped = min(textureLod(u_plant_density[nonuniformEXT(volume)], at + to_sun * ((float(i) + 0.7) * sun_span), 1.0).rgb, vec3(0.999));
            through *= pow(1.0 - stopped, vec3(sun_texels));
        }
    }

    // Lit from both sides as a leaf is, where it is leaf
    float facing = dot(normal, sun_direction);
    vec3 sun_color = light_color.rgb * exposure;
    vec3 sunlit = colour.rgb * sun_color * (max(facing, 0.0) + 0.5 * colour.a * max(-facing, 0.0)) * through;
    vec3 unlit = colour.rgb * exposure * (ambientLight(position, normal, 0.0) * mix(0.45, 1.0, baked.b) + pointLights(position, normal, 0.3));

    writeLit(unlit, sunlit, 0.0, position, 1.0);

}
