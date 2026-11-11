// Set 0 of every scene pipeline : the view (renderer/view_ubo.h) and the lights of the frame
// (renderer/lighting/lights.h), and the sky that follows from them (logic/sky/sky.h, mirrored here)

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos;       // w : time
    vec4 tree_shade;    // x : strength, y : 1 / width of the map in metres, z : distance where it ends
    mat4 inv_viewproj;
    mat4 prev_viewproj; // of the same eye, a frame before
    vec4 view_lighting; // x : traced range in metres, y : sun rays a pixel, z : shadowed torches a pixel
};

struct PointLight {
    vec4 position;      // w : radius
    vec4 color;         // rgb : light a metre away
};

const int max_point_lights = 8;

layout(std140, set = 0, binding = 3) uniform Lights {
    vec4 light_direction;   // xyz : towards the light that casts shadows (the sun, the moon at night), w : radius of its disc, radians
    vec4 light_color;       // rgb : its light on a surface facing it, w : 1 the sun, 0 the moon
    vec4 sky_zenith;
    vec4 sky_horizon;       // w : 0 the day, 1 the night
    vec4 sky_glow;          // rgb : around the sun, w : how tight
    vec4 sun_disc;          // xyz : towards the sun, w : how bright its disc is drawn
    vec4 moon_disc;
    vec4 ambient_sky;       // rgb : light of the sky on a surface facing up, w : sky light for a sky colour
    vec4 ambient_ground;    // rgb : light the ground sends back up, w : number of point lights
    vec4 light_frame;       // x : frames drawn so far, y : seconds
    PointLight point_lights[max_point_lights];
};

// The light that casts shadows was a constant of each shader
#define sun_direction (light_direction.xyz)

// Colour of the sky in a direction, without the discs of the sun and of the moon
vec3 skyColor(vec3 direction) {
    float up = clamp(direction.y, 0.0, 1.0);
    vec3 color = mix(sky_horizon.rgb, sky_zenith.rgb, sqrt(up));
    float toward = max(dot(direction, sun_disc.xyz), 0.0);
    float low = (1.0 - up) * (1.0 - up);
    return color + sky_glow.rgb * (pow(toward, sky_glow.w) + 0.35 * low * toward * toward);
}

// What a ray that misses everything brings back : the sky lights a little less than it shows, and greyer
vec3 skyRadiance(vec3 direction) {
    vec3 color = skyColor(direction);
    return mix(color, vec3(dot(color, vec3(0.2126, 0.7152, 0.0722))), 0.5) * ambient_sky.w;
}

// What is far fades into the sky behind it
float fogAmount(float far) {
    return 1.0 - exp(-far * 0.0006);
}

vec3 fogColor(vec3 view) {
    return skyColor(normalize(vec3(view.x, max(view.y, 0.03), view.z)));
}
