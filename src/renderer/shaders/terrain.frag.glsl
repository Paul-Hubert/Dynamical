#version 460
#extension GL_GOOGLE_include_directive : require

// The ground. Seen from above : earth (under the grass of the grass pass), and patches of mud, stones and
// sand where the soil map of the chunk says so (logic/terrain/soil.h). Steep faces : rock, laid on the two
// vertical planes. Stones again at the foot of the rock, where the ground gets steep.
// Layers are not faded into each other : each has a height (ground_import.py), and where two meet the
// higher one shows. Stones come out of the earth and of the sand by their tops, mud fills the hollows of
// what it covers, the ledges of the rock keep the earth (docs/ground.md)

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;

// Without the ground textures. Layer 0 : earth under the grass, layer 1 : rock
layout(set = 1, binding = 0) uniform sampler2DArray u_color;

// Ground textures, a layer each : mud, stones, sand (as the channels of the soil maps), rock, earth.
// u_ground_color : albedo. u_ground_detail : rg normal (x and z of the world when u is along x and
// v along z, 0.5 flat), b height, a occlusion
layout(set = 1, binding = 1) uniform sampler2DArray u_ground_color;
layout(set = 1, binding = 2) uniform sampler2DArray u_ground_detail;

const int layer_mud = 0;
const int layer_stones = 1;
const int layer_sand = 2;
const int layer_rock = 3;
const int layer_earth = 4;
const int ground_layers = 5;

// Only pushed back, under the grass : keeps the early depth test
layout(depth_greater) out float gl_FragDepth;

layout(push_constant) uniform Args {
    vec4 chunk;         // of each draw. xy : origin on the ground, z : layer of its soil map (< 0 : none), w : texels per metre
    float tiles[ground_layers];         // tiles per metre
    float smoothness[ground_layers];
    float under_grass;  // height of the grass that the grass pass draws over the flat ground, 0 without it
    float library;      // 0 : no ground textures (flat colours, the earth and the rock of u_color)
    float scree;        // stones at the foot of the rock
    float relief[3];    // depth of the relief of the mud, the stones and the sand, in metres : 0 flat
};

// The view, the lights, the shade of the plants (treeShade), outColor
#include "include/scene.glsl"

// Mud (r), stones (g) and sand (b) over each chunk, a layer each : 0.5 on the edge of a patch, more
// inside. a : how tall the grass is next to them (renderer/terrain/soil_maps.h). Texel (i, j) is on the vertices i, j cubes from the origin of the chunk
layout(set = 0, binding = 2) uniform sampler2DArray u_soil;

// The soil maps have a texel every 4 m : a patch would have straight edges. The maps are read a few
// metres off (never more than their border of one texel), further over tens of metres and less over one,
// and the edges wind. The same in grass.frag.glsl
const float soil_border = 1.0;
vec2 soilWarp(vec2 ground) {
    vec2 large = vec2(shadeNoise(ground * 0.13 + 3.1), shadeNoise(ground * 0.13 + 61.7)) - 0.5;
    vec2 middle = vec2(shadeNoise(ground * 0.47 + 17.0), shadeNoise(ground * 0.47 + 93.2)) - 0.5;
    vec2 small = vec2(shadeNoise(ground * 1.9 + 5.0), shadeNoise(ground * 1.9 + 37.9)) - 0.5;
    return 4.4 * large + 2.2 * middle + 0.7 * small;
}

// How much of the grass grows on a slope, by how far up its normal points : all of it on level ground, none
// where it gets steep (earth, then rock). The normal goes straight from a vertex of the terrain to the
// next : a threshold on it follows the triangles, and the earth of a hillside showed as polygons 4 m
// wide. A noise of a few metres winds the line. The same in grass.frag.glsl, which thins the blades by it
float slopeGrass(vec2 ground, float up) {
    float winding = 0.6 * shadeNoise(ground * 0.19 + 7.3) + 0.4 * shadeNoise(ground * 0.61 + 41.9) - 0.5;
    return smoothstep(0.71, 0.8, up + 0.1 * winding);
}

// Mud, stones and sand at a point of the ground of this chunk, from its soil map. a : height of the grass
vec4 soilAt(vec2 ground, float layer, float texels_per_metre) {
    vec2 texel = (ground - chunk.xy) * texels_per_metre + soil_border + 0.5;
    return textureLod(u_soil, vec3(texel / vec2(textureSize(u_soil, 0).xy), layer), 0.0);
}

// A tile of a few metres seen from far shows its repeats as a grid : the textures are read on tiles
// 1 / wide_scale times larger there (wide : 0 up close, 1 far), and blurred further still
const float wide_scale = 1.0 / 3.1;
float wide = 0.0;
float blur = 1.0;

// Normal, height and occlusion of a ground texture at a point of the plane it is laid on (metres).
// dx, dy : the pixel on that plane (the layers are read in branches)
vec4 groundDetail(int layer, vec2 at, vec2 dx, vec2 dy) {
    float tile = tiles[layer];
    vec4 close = vec4(0.0), away = vec4(0.0);
    if(wide < 1.0) close = textureGrad(u_ground_detail, vec3(at * tile, float(layer)), dx * tile, dy * tile);
    tile *= wide_scale;
    if(wide > 0.0) away = textureGrad(u_ground_detail, vec3(at * tile + 0.37, float(layer)), dx * (tile * blur), dy * (tile * blur));
    return mix(close, away, wide);
}

vec3 groundColor(int layer, vec2 at, vec2 dx, vec2 dy) {
    float tile = tiles[layer];
    vec3 close = vec3(0.0), away = vec3(0.0);
    if(wide < 1.0) close = textureGrad(u_ground_color, vec3(at * tile, float(layer)), dx * tile, dy * tile).rgb;
    tile *= wide_scale;
    if(wide > 0.0) away = textureGrad(u_ground_color, vec3(at * tile + 0.37, float(layer)), dx * (tile * blur), dy * (tile * blur)).rgb;
    return mix(close, away, wide);
}

// The relief of the mud, the stones and the sand (logic/terrain/ground_relief.h) : their height maps were
// built as geometry, and rays baked on it like on the grass (grass.frag.glsl, logic/grass/grass_bake.h).
// u_relief : height of the hit of the oblique rays, a layer per azimuth of each elevation, the three
// layers of the soil one after the other. u_relief_top : of the rays from the top, a layer each
layout(set = 1, binding = 3) uniform sampler2DArray u_relief;
layout(set = 1, binding = 4) uniform sampler2DArray u_relief_top;

layout(constant_id = 0) const int relief_angles_0 = 48;
layout(constant_id = 1) const int relief_angles_1 = 40;
layout(constant_id = 2) const int relief_angles_2 = 32;
layout(constant_id = 3) const int relief_angles_3 = 24;
layout(constant_id = 4) const float relief_low_from = 1.0;
layout(constant_id = 5) const float relief_low_to = 0.5;

const float relief_pi = 3.14159265359;
const float relief_max_reach = 3.0;     // grass_max_reach

// Of the pixel, set in main
vec3 r_view = vec3(0.0, -1.0, 0.0);
vec3 r_normal = vec3(0.0, 1.0, 0.0);

float reliefReach(int elevation) {
    return elevation == 0 ? relief_max_reach : 1.0 / tan(float(elevation) * relief_pi / 8.0);
}

// Rays of one elevation through a point of the tile, the two azimuths around the heading blended
float reliefRays(int layer, int elevation, vec2 point, vec2 heading, vec2 dx, vec2 dy) {
    int count = elevation == 0 ? relief_angles_0 : elevation == 1 ? relief_angles_1 : elevation == 2 ? relief_angles_2 : relief_angles_3;
    int first = elevation == 0 ? 0 : elevation == 1 ? relief_angles_0 : elevation == 2 ? relief_angles_0 + relief_angles_1
        : relief_angles_0 + relief_angles_1 + relief_angles_2;
    first += layer * (relief_angles_0 + relief_angles_1 + relief_angles_2 + relief_angles_3);
    float slice = atan(heading.y, heading.x) / (2.0 * relief_pi) * float(count) - 0.5;
    float lower = floor(slice);
    vec2 layers = float(first) + mod(lower + vec2(0.0, 1.0), float(count));
    return mix(textureGrad(u_relief, vec3(point, layers.x), dx, dy).r, textureGrad(u_relief, vec3(point, layers.y), dx, dy).r, slice - lower);
}

// Where the view ray meets the relief of a layer of the soil, as the point of the ground the textures
// are read at : the plane that is drawn is the top of the relief, the ray goes on under it. The baked rays
// through the point where it crosses the middle of the relief, of the two baked elevations around its
// own, say how far before or after that point the stone is (lookup in grass.frag.glsl).
// sunk : how deep under the plane the hit is, in metres
vec2 reliefAt(int layer, vec2 ground, vec2 dx, vec2 dy, float fade, out float sunk) {

    sunk = 0.0;
    float depth = relief[layer] * fade;
    float flat_speed = length(r_view.xz);
    if(depth <= 1e-4 || flat_speed < 1e-4) {
        return ground;
    }

    float tile = 1.0 / tiles[layer];
    float sink = max(-dot(r_view, r_normal) / r_normal.y, 0.0);
    // A ray that would cross more than relief_max_reach tiles is looked up as if it went down faster
    float fall = max(max(sink, flat_speed * depth / (tile * relief_max_reach)), 1e-6);
    vec2 along = r_view.xz / flat_speed;
    float reach = max(flat_speed * depth / (fall * tile), 1e-3);
    float flight = min(flat_speed * depth / (max(sink, 1e-6) * tile), 1e3);

    vec2 middle = (ground + r_view.xz * (0.5 * depth / fall)) / tile;
    vec2 tile_dx = dx / tile, tile_dy = dy / tile;

    int lower = flight >= reliefReach(1) ? 0 : flight >= reliefReach(2) ? 1 : flight >= reliefReach(3) ? 2 : 3;
    float from_reach = lower > 0 ? reliefReach(lower) : min(relief_max_reach / max(relief_low_from - relief_low_to, 1e-3), 1e3);
    float to_reach = lower < 3 ? reliefReach(lower + 1) : 0.0;
    float weight = clamp((from_reach - flight) / (from_reach - to_reach), 0.0, 1.0);

    float rays = reliefRays(layer, lower, middle, along, tile_dx, tile_dy);
    float ahead = (0.5 - rays) * reliefReach(lower);
    float hit = clamp(0.5 - ahead / reach, 0.0, 1.0);
    if(weight > 0.002) {
        float next_ahead = 0.0, next_hit;
        if(lower == 3) {
            next_hit = textureGrad(u_relief_top, vec3(middle, float(layer)), tile_dx, tile_dy).r;
        } else {
            float next = reliefRays(layer, lower + 1, middle, along, tile_dx, tile_dy);
            next_ahead = (0.5 - next) * reliefReach(lower + 1);
            next_hit = clamp(0.5 - next_ahead / reach, 0.0, 1.0);
        }
        hit = mix(hit, next_hit, weight);
        ahead = mix(ahead, next_ahead, weight);
    }

    sunk = (1.0 - hit) * depth;
    return (middle + along * ahead) * tile;

}

// What shows at this pixel so far
vec3 s_color;
vec3 s_bump = vec3(0.0);    // added to the normal of the terrain
float s_occlusion = 1.0;
float s_gloss = 0.0;
float s_height = 0.5;
float s_bare = 0.0;         // part that is one of the soil layers : no earth, no grass
float s_sunk = 0.0;         // how deep under the plane the relief of the layer that shows was met, in metres

// One layer of the soil over what is there. inside : how far in its patch the pixel is, 0 on the edge,
// in the range of the soil maps. The edge is moved by `spread` of that range by the heights : by the
// layer's own (fills 0, it comes out by its tops) or by the one of what it covers (fills 1, it lies in
// the hollows). soft : width of the edge itself
void soilLayer(int layer, float inside, float spread, float soft, float fills, vec2 ground, vec2 dx, vec2 dy) {

    if(inside < -(0.7 * spread + soft)) return;

    // The textures are read where the view ray meets the relief, not where it meets the plane. Not on
    // the large tiles of far away
    float sunk;
    vec2 flat_ground = ground;
    ground = reliefAt(layer, ground, dx, dy, 1.0 - wide, sunk);

    vec4 detail = groundDetail(layer, ground, dx, dy);

    float lift = mix(detail.b - 0.5, 0.5 - s_height + 0.4 * (detail.b - 0.5), fills);
    float alpha = smoothstep(-soft, soft, inside + spread * lift);
    if(alpha <= 0.0) return;

    // The tile repeats every few metres : lighter and darker over tens of metres
    vec3 albedo = groundColor(layer, ground, dx, dy);
    albedo *= 0.84 + 0.32 * shadeNoise(flat_ground * 0.11 + 13.0 * float(layer));

    vec2 slope = detail.rg * 2.0 - 1.0;
    s_color = mix(s_color, albedo, alpha);
    s_bump = mix(s_bump, vec3(slope.x, 0.0, slope.y), alpha);
    s_occlusion = mix(s_occlusion, detail.a, alpha);
    // What lies in the hollows is wet : water stands in its own
    s_gloss = mix(s_gloss, smoothness[layer] * mix(1.0, 1.0 - 0.6 * detail.b, fills), alpha);
    s_height = mix(s_height, detail.b, alpha);
    s_bare = max(s_bare, alpha);
    s_sunk = mix(s_sunk, sunk, alpha);

}

void main() {

    vec3 normal = normalize(v_normal);
    vec3 to_pixel = v_position - viewpos.xyz;
    float far = length(to_pixel);
    vec3 view = to_pixel / far;
    wide = smoothstep(40.0, 120.0, far);
    blur = max(far / 160.0, 1.0);
    r_view = view;
    r_normal = normal;

    // Derivatives are taken here : the layers are read in branches
    vec3 position_dx = dFdx(v_position), position_dy = dFdy(v_position);
    vec2 ground = v_position.xz;
    vec2 ground_dx = position_dx.xz, ground_dy = position_dy.xz;

    // The earth : under the grass, and what shows where the grass gets thin
    if(library > 0.5) {
        vec4 detail = groundDetail(layer_earth, ground, ground_dx, ground_dy);
        vec2 slope = detail.rg * 2.0 - 1.0;
        s_color = groundColor(layer_earth, ground, ground_dx, ground_dy) * (0.84 + 0.32 * shadeNoise(ground * 0.11 + 57.0));
        s_bump = vec3(slope.x, 0.0, slope.y);
        s_occlusion = detail.a;
        s_gloss = smoothness[layer_earth];
        s_height = detail.b;
    } else {
        s_color = textureGrad(u_color, vec3(ground / 3., 0), ground_dx / 3., ground_dy / 3.).rgb;
    }

    // The patches of the soil map, and how much grass there is next to them (grass.vert.glsl,
    // grass.frag.glsl) : shorter towards a patch, blade by blade over its edge
    vec3 inside = vec3(-1.0);
    float grassy = 1.0, grass_tall = 1.0;
    if(chunk.z >= 0.0) {
        vec3 soil = soilAt(ground + soilWarp(ground), chunk.z, chunk.w).rgb;
        inside = soil - 0.5;
        grassy = 1.0 - smoothstep(0.4, 0.6, max(soil.r, max(soil.g, soil.b)));
        // The shell of the grass has its height from the map on its vertices
        grass_tall = soilAt(ground, chunk.z, chunk.w).a;
    }

    // Stones fallen from the rock : where the ground gets steep, before the faces, here and there
    float foot = smoothstep(0.6, 0.7, normal.y) * (1.0 - smoothstep(0.78, 0.88, normal.y));
    foot *= scree * smoothstep(0.45, 0.7, 0.7 * shadeNoise(ground * 0.045 + 41.0) + 0.3 * shadeNoise(ground * 0.17 + 7.0));
    inside.g = max(inside.g, mix(-0.4, 0.16, foot) + 0.2 * (shadeNoise(ground * 1.3 + 23.0) - 0.5));

    // Sand at the bottom, stones on it, mud in the hollows of both
    float soft = 0.012 + 0.0006 * far;
    soilLayer(layer_sand, inside.b, 0.1, 3.0 * soft, 0.0, ground, ground_dx, ground_dy);
    soilLayer(layer_stones, inside.g, 0.24, soft, 0.0, ground, ground_dx, ground_dy);
    soilLayer(layer_mud, inside.r, 0.2, soft, 1.0, ground, ground_dx, ground_dy);

    // Rock on the steep faces : each of the two vertical planes where the face looks its way, the one
    // whose rock is higher where they meet, instead of both seen through each other
    float level = smoothstep(0.55, 0.8, normal.y);
    if(level < 0.999) {

        float facing_x = normal.x * normal.x / (normal.x * normal.x + normal.z * normal.z + 0.0001);
        vec3 rock, rock_bump = vec3(0.0);
        float rock_height = 0.5, rock_occlusion = 1.0;

        if(library > 0.5) {
            // u along the face, v down
            vec2 at_x = vec2(v_position.z, -v_position.y), at_z = vec2(v_position.x, -v_position.y);
            vec2 dx_x = vec2(position_dx.z, -position_dx.y), dy_x = vec2(position_dy.z, -position_dy.y);
            vec2 dx_z = vec2(position_dx.x, -position_dx.y), dy_z = vec2(position_dy.x, -position_dy.y);

            vec4 detail_x = groundDetail(layer_rock, at_x, dx_x, dy_x);
            vec4 detail_z = groundDetail(layer_rock, at_z, dx_z, dy_z);
            float plane = smoothstep(-0.1, 0.1, facing_x - 0.5 + 0.35 * (detail_x.b - detail_z.b));

            rock = vec3(0.0);
            if(plane < 1.0) rock += (1.0 - plane) * groundColor(layer_rock, at_z, dx_z, dy_z);
            if(plane > 0.0) rock += plane * groundColor(layer_rock, at_x, dx_x, dy_x);
            rock *= 0.84 + 0.32 * shadeNoise(v_position.xz * 0.07 + v_position.y * 0.13 + 71.0);
            vec2 slope_x = detail_x.rg * 2.0 - 1.0, slope_z = detail_z.rg * 2.0 - 1.0;
            rock_bump = mix(vec3(slope_z.x, -slope_z.y, 0.0), vec3(0.0, -slope_x.y, slope_x.x), plane);
            rock_height = mix(detail_z.b, detail_x.b, plane);
            rock_occlusion = mix(detail_z.a, detail_x.a, plane);
        } else {
            rock = max(mix(
                    textureGrad(u_color, vec3(v_position.yx / 3., 1), position_dx.yx / 3., position_dy.yx / 3.),
                    textureGrad(u_color, vec3(v_position.yz / 3., 1), position_dx.yz / 3., position_dy.yz / 3.),
                    facing_x
                ), 0).rgb;
        }

        // The ground stays on what is level enough, and first in the hollows of the rock
        float kept = smoothstep(-0.08, 0.08, level - 0.5 + 0.3 * (s_height - rock_height));
        s_color = mix(rock, s_color, kept);
        s_bump = mix(rock_bump, s_bump, kept);
        s_occlusion = mix(rock_occlusion, s_occlusion, kept);
        s_gloss = mix(smoothness[layer_rock], s_gloss, kept);
        s_bare *= kept;

    }

    // The relief of the textures
    vec3 lit_normal = normalize(normal + 1.2 * s_bump);

    // Under the plants : little sun, and less of the sky
    float shade = treeShade(v_position, 0.0);
    float light = max(dot(lit_normal, sun_direction), 0.0);
    vec3 sunlit = s_color * light_color.rgb * (light * mix(1.0, s_occlusion, 0.5));
    vec3 unlit = s_color * (ambientLight(v_position, lit_normal, shade) * s_occlusion + pointLights(v_position, lit_normal, 0.0));

    // Wet mud shines, stones a little
    vec3 half_way = normalize(sun_direction - view);
    float shine = 0.4 * s_gloss * s_gloss * pow(max(dot(lit_normal, half_way), 0.0), 8.0 + 120.0 * s_gloss);
    sunlit += light_color.rgb * (1.4 * shine * smoothstep(0.0, 0.15, dot(normal, sun_direction)));

    // Under the grass (same slopes as grass.vert.glsl) : in its shade, and behind it. The grass pass
    // only knows the ground as a plane under each pixel, its hits can end up a little under the terrain :
    // the terrain steps back by the height of the grass and lets them through. None of that on the soil
    // The relief is under the plane : what stands on the ground hides it by that much
    gl_FragDepth = gl_FragCoord.z;
    float pushed = 0.0;     // metres along the view
    if(s_sunk > 0.0) {
        pushed = s_sunk / max(-view.y, 0.05);
    }
    if(under_grass > 0.0) {
        float covered = slopeGrass(ground, normal.y);
        // Far away the grass ends on one line (grass.frag.glsl, blades are not told apart) : no half
        // shade past it
        covered = mix(covered, smoothstep(0.45, 0.55, covered), smoothstep(10.0, 40.0, far));
        float grass_over = covered * grassy * (1.0 - 0.7 * s_bare);
        float under = 1.0 - 0.8 * grass_over;
        unlit *= under;
        sunlit *= under;
        // Seen from above, that is the ground between the blades. Seen along the ground it is not : a
        // ray only gets under whole grass without meeting a blade where it came in under the edge of the
        // shell (the lower edge of the grass of a hillside seen from below, grass thinned out in front
        // of thicker grass), and would have met the blades from the side. The ground is drawn as them :
        // the green of the grass half way up, in its own shade
        float along = grass_over * (1.0 - smoothstep(0.25, 0.6, -dot(view, normal)));
        if(along > 0.0) {
            const vec3 grass_side = vec3(0.13, 0.27, 0.065);
            unlit = mix(unlit, grass_side * 0.8 * ambientLight(v_position, normal, shade), along);
            sunlit = mix(sunlit, grass_side * 0.8 * light_color.rgb * max(dot(normal, sun_direction), 0.0), along);
        }
        pushed = max(pushed, under_grass * covered * grass_tall * smoothstep(0.0, 0.15, grassy));
    }
    if(pushed > 0.0) {
        vec4 clip = viewproj * vec4(v_position + view * pushed, 1.0);
        gl_FragDepth = clamp(max(clip.z / clip.w, gl_FragDepth), 0.0, 1.0);
    }

    // The ray to the sun starts where the depth says the pixel is : pushed back, that is under the
    // ground, which then hides the sun from itself (the stones were black between their tops, the ground
    // seen through the grass was black). It starts as far over that point as the push went under the
    // triangle that is drawn, and a little more : the point is as far to the side as it was pushed,
    // where the next triangle may be higher
    vec3 flat_normal = cross(position_dx, position_dy);
    flat_normal *= sign(flat_normal.y) / max(length(flat_normal), 1e-12);
    float under_ground = pushed * max(-dot(view, flat_normal), 0.0) / max(flat_normal.y, 0.2);

    writeLit(unlit, sunlit, shade, v_position, liftAlpha(terminatorLift(position_dx, position_dy, normal, 4.0) + under_ground + 0.25 * pushed));

}
