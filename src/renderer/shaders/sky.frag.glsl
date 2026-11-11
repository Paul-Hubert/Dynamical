#version 460
#extension GL_GOOGLE_include_directive : require

// The sky behind everything : its colour (include/view.glsl, which the fog and the rays that miss read
// too), the discs of the sun and of the moon, stars at night

layout(location = 0) in vec3 v_direction;

#include "include/scene.glsl"

// Drawn a little larger than they are : half a degree is a few pixels
const float disc_radius = 0.008;

float disc(vec3 direction, vec3 towards) {
    float angle = acos(clamp(dot(direction, towards), -1.0, 1.0));
    return 1.0 - smoothstep(disc_radius * 0.85, disc_radius * 1.15, angle);
}

void main() {

    vec3 direction = normalize(v_direction);
    vec3 color = skyColor(direction);
    float night = sky_horizon.w;

    // Stars : one in a few hundred cells of the sphere, where the sky is dark and clear of the horizon
    if(night > 0.0 && direction.y > 0.0) {
        vec3 cell = floor(direction * 260.0);
        float h = fract(sin(dot(cell, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        float star = step(0.997, h) * smoothstep(0.6, 1.0, night) * smoothstep(0.0, 0.25, direction.y);
        color += vec3(0.8, 0.85, 1.0) * star * (0.25 + 0.75 * fract(h * 917.0));
    }

    vec3 sun_tint = mix(vec3(1.0, 0.42, 0.12), vec3(1.0, 0.97, 0.9), smoothstep(0.0, 0.25, sun_disc.y));
    color += sun_tint * (4.0 * disc(direction, sun_disc.xyz) * sun_disc.w);

    float moon = disc(direction, moon_disc.xyz) * moon_disc.w;
    color = mix(color, vec3(0.78, 0.8, 0.86) * (0.35 + 0.65 * night), moon);

    writeUnlit(color);

}
