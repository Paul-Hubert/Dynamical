#version 460
#extension GL_GOOGLE_include_directive : require

// Grass without blade geometry. The pixel is on a shell at the top of the grass; its view ray is looked up
// in rays baked against a tile of grass blades (logic/grass/grass_bake.h, mirrored on the CPU by lookupGrass) :
//  - u_oblique : rays at 22.5, 45 and 67.5 degrees under the horizon and, before them, the lowest rays (9.5
//    degrees : the views along the ground), for a position in the tile and an azimuth : a layer for each
//    azimuth of each elevation
//  - u_top : rays going straight down
// They hold the height of the hit (0 ground, 1 top of the grass; for the lowest rays, the part of their way
// that was left), which every layer reads. Its normal (octahedral, decodeNormal) and a random value per
// blade (0.5 on the ground) and its colour are in textures of their own (u_*_normal, u_*_id, u_*_colour), read once for the closest
// hit (logic/grass/grass_textures.h).
// All are trilinear and anisotropic : the shader gives them the pixel on the ground (textureGrad).
//
// Everything is done in root space : where on the ground the blade under a point is rooted. Wind and terrain
// normal lean the blades, which shears that space around the ground : roots do not move. A baked ray is
// known by where it crosses the middle of the grass, so the view ray is looked up where it crosses the
// middle too : what the lookup gets wrong is shared between the top and the bottom of the grass instead of
// growing all the way down.
//
// The grass is several layers : the same tile turned, shifted, scaled and bent differently (docs/grass/plan.md).

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in float v_height;     // of the grass here : the shell is that much above the ground
layout(location = 3) in vec3 v_curve;       // how the ground bends : second derivatives of its height, xx, xz, zz

// The hit is never in front of the shell : keeps the early depth test
layout(depth_greater) out float gl_FragDepth;

layout(push_constant) uniform Args {
    vec4 base_normal;       // xyz : direction the blades grow in, w : how much they follow the terrain normal
    vec4 chunk;             // of the height map and of the soil map (grass.vert.glsl)
    float map_scale;
    float tile_size;
    float grass_height;
    float patch_size;
    float patch_amount;
    float noise_frequency;
    float noise_amplitude;
    float wind_strength;
    float wind_speed;
    float top_start;
    int layer_bits;         // tall layers in the low byte, how much the patches of the species overlap over it (thousandths)
    float layer_spread;
    float layer_patch;
    float undergrowth;
    float colour_variation;
    float dryness;
    float curve;
    float shadow;
    float canopy;
    float silhouette;
    float azimuth_dither;
    float baked_colour;
    float species_size;
    float species_low;
};

layout(set = 1, binding = 0) uniform sampler2DArray u_oblique;
layout(set = 1, binding = 1) uniform sampler2DArray u_top;
layout(set = 1, binding = 2) uniform sampler2D u_noise;
layout(set = 1, binding = 4) uniform sampler2DArray u_oblique_normal;
layout(set = 1, binding = 5) uniform sampler2DArray u_oblique_id;
layout(set = 1, binding = 6) uniform sampler2DArray u_top_normal;
layout(set = 1, binding = 7) uniform sampler2DArray u_top_id;
layout(set = 1, binding = 8) uniform sampler2DArray u_oblique_colour;
layout(set = 1, binding = 9) uniform sampler2DArray u_top_colour;
layout(set = 1, binding = 10) uniform sampler2D u_species;

// How much the patches of the kinds of plants overlap, 0 to 1 (GrassSpeciesParams::overlap) : the kinds within
// species_margin of the best noise grow together, small and tall plants over species_band of the heights
#define species_overlap (float(layer_bits >> 8) / 1000.0)
const float species_margin = 1.0;
const float species_band = 0.45;

// The view, the lights, the shade of the plants (treeShade), outColor
#include "include/scene.glsl"

const float pi = 3.14159265359;

// Rays closer to the horizontal are looked up as if they crossed this many tiles (grass_max_reach)
const float max_reach = 3.0;

// Azimuths of each elevation of the oblique rays (GrassRaycast::angles) : their layers follow each other
layout(constant_id = 0) const int angles_0 = 96;
layout(constant_id = 1) const int angles_1 = 64;
layout(constant_id = 2) const int angles_2 = 64;
layout(constant_id = 3) const int angles_3 = 32;

// Heights where the lowest rays start and are after max_reach tiles (GrassRaycast::low_from, low_to)
layout(constant_id = 4) const float low_from = 1.0;
layout(constant_id = 5) const float low_to = 0.5;

// The kinds of tiles (GrassKind), their layers one kind after the other in the textures of the rays :
// metres each was modelled at (GrassModel::tile_size, height), how it is chosen (GrassKindInfo)
const int kinds = 10;
layout(constant_id = 6) const float kind_tile_0 = 1.0;
layout(constant_id = 7) const float kind_tile_1 = 1.0;
layout(constant_id = 8) const float kind_tile_2 = 1.0;
layout(constant_id = 9) const float kind_tile_3 = 1.0;
layout(constant_id = 10) const float kind_tile_4 = 1.0;
layout(constant_id = 11) const float kind_tile_5 = 1.0;
layout(constant_id = 12) const float kind_tile_6 = 1.0;
layout(constant_id = 13) const float kind_tile_7 = 1.0;
layout(constant_id = 14) const float kind_tile_8 = 1.0;
layout(constant_id = 15) const float kind_tile_9 = 1.0;
layout(constant_id = 16) const float kind_height_0 = 1.0;
layout(constant_id = 17) const float kind_height_1 = 1.0;
layout(constant_id = 18) const float kind_height_2 = 1.0;
layout(constant_id = 19) const float kind_height_3 = 1.0;
layout(constant_id = 20) const float kind_height_4 = 1.0;
layout(constant_id = 21) const float kind_height_5 = 1.0;
layout(constant_id = 22) const float kind_height_6 = 1.0;
layout(constant_id = 23) const float kind_height_7 = 1.0;
layout(constant_id = 24) const float kind_height_8 = 1.0;
layout(constant_id = 25) const float kind_height_9 = 1.0;
layout(constant_id = 26) const float kind_abundance_0 = 0.0;
layout(constant_id = 27) const float kind_abundance_1 = 0.0;
layout(constant_id = 28) const float kind_abundance_2 = 0.0;
layout(constant_id = 29) const float kind_abundance_3 = 0.0;
layout(constant_id = 30) const float kind_abundance_4 = 0.0;
layout(constant_id = 31) const float kind_abundance_5 = 0.0;
layout(constant_id = 32) const float kind_abundance_6 = 0.0;
layout(constant_id = 33) const float kind_abundance_7 = 0.0;
layout(constant_id = 34) const float kind_abundance_8 = 0.0;
layout(constant_id = 35) const float kind_abundance_9 = 0.0;
layout(constant_id = 36) const int small_mask = 0;      // a bit per kind : small plants, where the grass is short
layout(constant_id = 37) const int forced_kind = -1;    // that kind everywhere

float kindTile(int kind) {
    float values[kinds] = float[](kind_tile_0, kind_tile_1, kind_tile_2, kind_tile_3, kind_tile_4, kind_tile_5, kind_tile_6, kind_tile_7, kind_tile_8, kind_tile_9);
    return values[kind];
}

float kindHeight(int kind) {
    float values[kinds] = float[](kind_height_0, kind_height_1, kind_height_2, kind_height_3, kind_height_4, kind_height_5, kind_height_6, kind_height_7, kind_height_8, kind_height_9);
    return values[kind];
}

float kindAbundance(int kind) {
    float values[kinds] = float[](kind_abundance_0, kind_abundance_1, kind_abundance_2, kind_abundance_3, kind_abundance_4, kind_abundance_5, kind_abundance_6, kind_abundance_7, kind_abundance_8, kind_abundance_9);
    return values[kind];
}

// Kind the lookups below read : set before them
int ray_kind = 0;

const int elevations = 4;
const float elevation_step = pi / 8.0;

// Up to three tall layers, and the undergrowth in the last place : short and dense.
// Tiles of different sizes only repeat together very far
// Mips are means of blades : thin ones fade before they are too small to see. Sharper, a little aliased
const float mip_bias = -0.8;

const int max_layers = 4;
const float layer_scales[max_layers] = float[](1.0, 1.37, 1.73, 0.55);
const float warp_frequencies[max_layers] = float[](1.0, 1.37, 0.73, 1.61);


// Mud, stones and sand over each chunk : 0.5 on the edge of a patch, more inside (renderer/terrain/soil_maps.h)
layout(set = 0, binding = 2) uniform sampler2DArray u_soil;

// The soil maps have a texel every 4 m : a patch would have straight edges. The maps are read a few
// metres off (never more than their border of one texel), further over tens of metres and less over one,
// and the edges wind. The same in terrain.frag.glsl
const float soil_border = 1.0;
vec2 soilWarp(vec2 ground) {
    vec2 large = vec2(shadeNoise(ground * 0.13 + 3.1), shadeNoise(ground * 0.13 + 61.7)) - 0.5;
    vec2 middle = vec2(shadeNoise(ground * 0.47 + 17.0), shadeNoise(ground * 0.47 + 93.2)) - 0.5;
    vec2 small = vec2(shadeNoise(ground * 1.9 + 5.0), shadeNoise(ground * 1.9 + 37.9)) - 0.5;
    return 4.4 * large + 2.2 * middle + 0.7 * small;
}

// How much of the grass grows on a slope, by how far up its normal points : all of it on level ground, none
// where it gets steep. The normal goes straight from a vertex of the terrain to the next : a threshold on
// it follows the triangles (the grass of a hillside ended on polygons 4 m wide). A noise of a few metres
// winds the line, and the blades go one by one over it, as they do into a patch of soil. The same in
// terrain.frag.glsl
float slopeGrass(vec2 ground, float up) {
    float winding = 0.6 * shadeNoise(ground * 0.19 + 7.3) + 0.4 * shadeNoise(ground * 0.61 + 41.9) - 0.5;
    return smoothstep(0.71, 0.8, up + 0.1 * winding);
}

// Mud, stones and sand at a point of the ground of this chunk, from its soil map. a : height of the grass
vec4 soilAt(vec2 ground, float layer, float texels_per_metre) {
    vec2 texel = (ground - chunk.xy) * texels_per_metre + soil_border + 0.5;
    return textureLod(u_soil, vec3(texel / vec2(textureSize(u_soil, 0).xy), layer), 0.0);
}

const vec3 root_color = vec3(0.04, 0.13, 0.03);
const vec3 tip_color = vec3(0.26, 0.5, 0.12);
const vec3 dry_root_color = vec3(0.18, 0.18, 0.07);
const vec3 dry_tip_color = vec3(0.52, 0.5, 0.22);

// encodeGrassNormal of grass_bake.cpp : +y in the middle of the square
vec3 decodeNormal(vec2 encoded) {
    vec2 e = encoded * 2.0 - 1.0;
    vec3 n = vec3(e.x, 1.0 - abs(e.x) - abs(e.y), e.y);
    float fold = max(-n.y, 0.0);
    n.x += n.x >= 0.0 ? -fold : fold;
    n.z += n.z >= 0.0 ? -fold : fold;
    return normalize(n);
}

int elevationAngles(int elevation) {
    return elevation == 0 ? angles_0 : elevation == 1 ? angles_1 : elevation == 2 ? angles_2 : angles_3;
}

int elevationFirst(int elevation) {
    return elevation == 0 ? 0 : elevation == 1 ? angles_0 : elevation == 2 ? angles_0 + angles_1 : angles_0 + angles_1 + angles_2;
}

// Tiles a baked ray crosses on its way (grassReach)
float elevationReach(int elevation) {
    return elevation == 0 ? max_reach : 1.0 / tan(float(elevation) * elevation_step);
}

// The two baked azimuths around a heading, for the rays of one elevation : their layers, and the weight
// of the second
void azimuthLayers(int elevation, vec2 heading, out vec2 layers, out float weight) {
    float count = float(elevationAngles(elevation));
    float slice = atan(heading.y, heading.x) / (2.0 * pi) * count - 0.5;
    float lower = floor(slice);
    layers = float(ray_kind * (angles_0 + angles_1 + angles_2 + angles_3) + elevationFirst(elevation)) + mod(lower + vec2(0.0, 1.0), count);
    weight = slice - lower;
}

// Rays of one elevation through a point of the tile, filtered over the pixel (dx, dy : its two axes on
// the ground, in tiles) : their height. push : how far the blend of the two azimuths goes towards one of
// them, which dither chooses with the blend weight as its odds
float obliqueRays(int elevation, vec2 point, vec2 heading, vec2 dx, vec2 dy, float push, float dither) {
    vec2 layers;
    float weight;
    azimuthLayers(elevation, heading, layers, weight);
    weight = mix(weight, step(dither, weight), push);
    if(weight < 0.002) {
        return textureGrad(u_oblique, vec3(point, layers.x), dx, dy).r;
    }
    if(weight > 0.998) {
        return textureGrad(u_oblique, vec3(point, layers.y), dx, dy).r;
    }
    return mix(textureGrad(u_oblique, vec3(point, layers.x), dx, dy).r, textureGrad(u_oblique, vec3(point, layers.y), dx, dy).r, weight);
}

// Normal as stored (xy) and blade id (z) of one baked azimuth, and the colour of the blade
vec3 obliqueTexel(vec3 where, vec2 dx, vec2 dy, out vec3 colour) {
    colour = textureGrad(u_oblique_colour, where, dx, dy).rgb;
    return vec3(textureGrad(u_oblique_normal, where, dx, dy).rg, textureGrad(u_oblique_id, where, dx, dy).r);
}

// The heights at one mip, without the pixel
float obliqueRaysLod(int elevation, vec2 point, vec2 heading, float lod) {
    vec2 layers;
    float weight;
    azimuthLayers(elevation, heading, layers, weight);
    return mix(textureLod(u_oblique, vec3(point, layers.x), lod).r, textureLod(u_oblique, vec3(point, layers.y), lod).r, weight);
}

// One layer : the baked rays through the same point of the middle of the grass, from the same side, of
// the two baked elevations around the ray's, or of the last one and the top over it. They are blended
// along the tiles they cross (their cotangent) : a view carried to a lower ray stays closer to it than
// to a higher one. reach : tiles the ray crosses as it is looked up, flight : before it was made to go
// down faster. The lowest rays answer for everything under them.
// hit : height of the hit on the view ray, baked : height where the baked ray met the blade (how deep in
// the grass), ahead : how far after the middle the blade is, in tiles.
// lower, weight : the baked elevation that was read and how much of the next one (or of the top) went in,
// for the normal and the blade (hitTexel).
// dx, dy : the pixel in the tile. dither : two values in 0..1, fixed for the pixel.
// push : blending two baked views shows a thin blade twice. The pixel is pushed that far towards one
// azimuth and one elevation, more often the nearer ones : the same blend over neighbouring pixels, one
// sharp view in each
void lookup(vec2 tile_middle, vec2 heading, float reach, float flight, vec2 dx, vec2 dy, float push, vec2 dither,
        out float hit, out float baked, out float ahead, out int lower, out float weight) {

    lower = flight >= elevationReach(1) ? 0 : flight >= elevationReach(2) ? 1 : flight >= elevationReach(3) ? 2 : 3;
    float from_reach = lower > 0 ? elevationReach(lower) : min(max_reach / max(low_from - low_to, 1e-3), 1e3);
    float to_reach = lower + 1 < elevations ? elevationReach(lower + 1) : 0.0;
    weight = clamp((from_reach - flight) / (from_reach - to_reach), 0.0, 1.0);
    bool to_top = lower == elevations - 1;
    if(to_top) {
        weight = smoothstep(min(top_start, 0.999), 1.0, weight);
    } else {
        weight = mix(weight, step(dither.y, weight), push);
        if(weight > 0.998) {
            lower++;
            weight = 0.0;
        }
    }
    reach = max(reach, 1e-3);

    // The baked ray met a blade `ahead` tiles further than the middle. Blades are mostly vertical :
    // the view ray meets it at the same place, higher or lower. The baked hit itself at its elevation
    float rays = obliqueRays(lower, tile_middle, heading, dx, dy, push, dither.x);
    ahead = (0.5 - rays) * elevationReach(lower);
    hit = clamp(0.5 - ahead / reach, 0.0, 1.0);
    baked = lower == 0 ? mix(low_to, low_from, rays) : rays;

    if(weight > 0.002) {
        float next;
        float next_ahead = 0.0, next_hit;
        if(to_top) {
            next = textureGrad(u_top, vec3(tile_middle, ray_kind), dx, dy).r;
            next_hit = next;
        } else {
            next = obliqueRays(lower + 1, tile_middle, heading, dx, dy, push, dither.x);
            next_ahead = (0.5 - next) * elevationReach(lower + 1);
            next_hit = clamp(0.5 - next_ahead / reach, 0.0, 1.0);
        }
        hit = mix(hit, next_hit, weight);
        baked = mix(baked, next, weight);
        ahead = mix(ahead, next_ahead, weight);
    }

}

// Normal as stored (xy) and blade id (z) of a hit, from the baked view that weighs the most in the height
// lookup read (lower, weight : what it gave back) : two views blended would have the colour of no blade
vec3 hitTexel(vec2 tile_middle, vec2 heading, vec2 dx, vec2 dy, float push, float dither, int lower, float weight, out vec3 colour) {
    if(weight > 0.5) {
        if(lower == elevations - 1) {
            colour = textureGrad(u_top_colour, vec3(tile_middle, ray_kind), dx, dy).rgb;
            return vec3(textureGrad(u_top_normal, vec3(tile_middle, ray_kind), dx, dy).rg, textureGrad(u_top_id, vec3(tile_middle, ray_kind), dx, dy).r);
        }
        lower++;
    }
    vec2 layers;
    float second;
    azimuthLayers(lower, heading, layers, second);
    second = mix(second, step(dither, second), push);
    return obliqueTexel(vec3(tile_middle, second > 0.5 ? layers.y : layers.x), dx, dy, colour);
}

// Part of the sun hidden at a point of a layer (tile space, at the part `height` of the layer), for
// light that comes down along `heading` and crosses `reach` tiles of the layer.
// The oblique rays answer for light at 45 degrees : the baked ray through the point from the side of
// the sun, carried to the sun ray like a view ray, must stop at the point itself. The top rays answer
// for light from above : the canopy must stay under the sun ray on its way up.
float sunShadow(vec2 point, float height, vec2 heading, float reach, float lod) {

    float steep = clamp((atan(1.0, reach) - pi / 4.0) / (pi / 4.0), 0.0, 1.0);

    float slanted_reach = max(reach, 0.3);
    vec2 middle = point - heading * slanted_reach * (0.5 - height);
    float baked = obliqueRaysLod(2, middle, heading, lod);
    float seen = clamp(0.5 - (0.5 - baked) / slanted_reach, 0.0, 1.0);
    float slanted = smoothstep(0.08, 0.3, seen - height);

    float above = 0.0;
    for(int k = 1; k <= 2; k++) {
        float y = mix(height, 1.0, float(k) / 2.5);
        float canopy = textureLod(u_top, vec3(point - heading * reach * (y - height), ray_kind), lod).r;
        above = max(above, smoothstep(0.05, 0.25, canopy - y));
    }

    return mix(slanted, above, steep);

}

// Which kinds of plants grow at a root, and how many of each (logic/grass/grass_species.h, grassSpeciesAt).
// Each kind has its own noise in octaves, a channel of a texture that repeats, read at three scales. The
// three kinds whose noises are the highest are the tall layers. Patches have no edge : a kind is as dense
// as its noise is over the next one's, from every plant of its tile half a blend width inside its patch to
// none as far outside (keep : the part of the plants that stay, the first ones of their tile's order, which
// are spread evenly). Where two patches meet each has half of its plants.
// Small kinds grow where the grass is short (tallness : 0 its shortest, 1 its tallest), tall ones where it
// is not : the height of the grass adds to the noise of ones and takes from the others' over a band
// around species_low, they blend like any two kinds.
// under : part of the short meadow grass that stays under the tall kinds, none under the small plants
void species(vec2 root, float tallness, out ivec3 kind, out vec3 keep, out float under) {
    if(forced_kind >= 0 || species_size <= 0.0) {
        kind = ivec3(max(forced_kind, 0));
        keep = vec3(1.0);
        under = ((small_mask >> kind.x) & 1) != 0 ? 0.0 : 1.0;
        return;
    }
    // How much the place is for the tall kinds, -1 to 1 over the band
    float band = max(species_band * species_overlap, 1e-3);
    // Gently over the middle of the band, where the noises still decide : the two groups blend like two kinds.
    // For good on its sides
    float width = max(species_margin * species_overlap, 1e-6);
    float across = (tallness - species_low) / band;
    float tall = width * clamp(across, -0.5, 0.5) + 2.0 * (clamp(across - 0.5, 0.0, 0.5) - clamp(-across - 0.5, 0.0, 0.5));

    vec2 at = root / (species_size * 4.0);
    vec4 read[3] = vec4[](
        textureLod(u_species, at, 0.0),
        textureLod(u_species, at * 1.37 + vec2(0.31, 0.67), 0.0),
        textureLod(u_species, at * 0.73 + vec2(0.59, 0.13), 0.0));
    kind = ivec3(-1);
    vec3 best = vec3(-1e9);
    float best_small = -1e9, best_tall = -1e9;
    for(int k = 0; k < kinds; k++) {
        bool small = ((small_mask >> k) & 1) != 0;
        float value = read[k / 4][k % 4] + kindAbundance(k) + (small ? -tall : tall);
        if(small) best_small = max(best_small, value);
        else best_tall = max(best_tall, value);
        if(value > best.x) {
            best = vec3(value, best.xy);
            kind = ivec3(k, kind.xy);
        } else if(value > best.y) {
            best.yz = vec2(value, best.y);
            kind.yz = ivec2(k, kind.y);
        } else if(value > best.z) {
            best.z = value;
            kind.z = k;
        }
    }
    // The first kind against the second, the others against the first
    keep = clamp(0.5 + vec3(best.x - best.y, best.y - best.x, best.z - best.x) / width, 0.0, 1.0);
    // Where no other kind is close the layers are all of the first one, as dense
    if(kind.y < 0 || keep.y <= 0.0) {
        kind.y = kind.x;
        keep.y = keep.x;
    }
    if(kind.z < 0 || keep.z <= 0.0) {
        kind.z = kind.x;
        keep.z = keep.x;
    }
    bool first_small = ((small_mask >> kind.x) & 1) != 0;
    under = first_small ? 0.0 : clamp(0.5 + (best_tall - best_small) / width, 0.0, 1.0);
}

// Id of the plant a layer met (hitTexel) : its rank in the order of its tile, 0 the first plant to 1 the last
float hitPlant(vec2 tile_middle, vec2 heading, vec2 dx, vec2 dy, float push, float dither, int lower, float weight) {
    if(weight > 0.5) {
        if(lower == elevations - 1) {
            return textureGrad(u_top_id, vec3(tile_middle, ray_kind), dx, dy).r;
        }
        lower++;
    }
    vec2 layers;
    float second;
    azimuthLayers(lower, heading, layers, second);
    second = mix(second, step(dither, second), push);
    return textureGrad(u_oblique_id, vec3(tile_middle, second > 0.5 ? layers.y : layers.x), dx, dy).r;
}

void main() {

    // No grass on the slopes : the shell comes down to the ground there (grass.vert.glsl), and the blades
    // thin out to none where the ground gets steep (slopeGrass, further down)
    vec3 ground_normal = normalize(v_normal);
    if(v_height < 0.03 * grass_height || slopeGrass(v_position.xz, ground_normal.y) <= 0.0) {
        discard;
    }

    vec3 to_pixel = v_position - viewpos.xyz;
    float view_distance = length(to_pixel);
    vec3 view = to_pixel / view_distance;

    // Direction of the blades : up, a bit with the terrain, and with the wind
    float gust = (v_position.x + v_position.z) / 10.0 + viewpos.w * wind_speed * 0.5 * pi;
    float flutter = dot(v_position.xz, vec2(0.31, -0.23)) + viewpos.w * wind_speed * 2.3;
    vec3 wind = wind_strength * (vec3(0.5 * sin(gust), 0, cos(gust)) + 0.3 * vec3(cos(flutter), 0, sin(flutter)));
    vec3 growth = normalize(base_normal.xyz) + base_normal.w * (ground_normal - vec3(0, 1, 0)) + wind * (1.0 - curve);
    float growth_up = max(dot(growth, ground_normal), 0.2);

    // Heights are measured straight up from the ground (the shell is v_height above it).
    // A point of a blade at the part y of its layer (0 root, 1 top), of height h, is
    // h * (lean * y + bend * y * y) from its root : part of the wind bends the blades instead of leaning them
    vec2 lean = ground_normal.y * growth.xz / growth_up;
    vec2 bend = ground_normal.y * wind.xz * curve / growth_up;

    // The ray is a curve in root space : it is looked up as its tangent at the middle of the layer
    vec2 slope = lean + bend;

    // Height the view ray loses per unit of distance
    float sink = max(-dot(view, ground_normal) / ground_normal.y, 0.0);

    // Mip from the size of the pixel on the ground, across and along the view (their geometric mean).
    // Not from the lookup coordinates, which jump between blades and where the azimuth wraps, nor from
    // the position, whose derivatives jump from a triangle to the next
    float pixel_angle = sqrt(length(dFdx(view)) * length(dFdy(view)));
    float incidence = max(-dot(view, ground_normal), 0.05);
    float pixel_across = view_distance * pixel_angle;
    float pixel_size = pixel_across / sqrt(incidence);
    float lod_metre = log2(max(pixel_size * float(textureSize(u_oblique, 0).x), 1e-6)) + mip_bias;

    // The baked rays are filtered over the pixel itself (anisotropic) : as wide as the view is far, and
    // longer along the view at grazing angles. In metres on the ground, with the same bias
    vec2 view_flat = dot(view.xz, view.xz) > 1e-8 ? normalize(view.xz) : vec2(1, 0);
    vec2 pixel_long = view_flat * (pixel_across / incidence * exp2(mip_bias));
    vec2 pixel_wide = vec2(-view_flat.y, view_flat.x) * (pixel_across * exp2(mip_bias));

    // Where the height patches of the layers are read : the root of the ray at the middle of the shell,
    // the same for every layer and close to the same from every view
    float shell_fall = max(max(sink, length(view.xz + sink * slope) * v_height / (tile_size * max_reach)), 1e-6);
    vec2 patch_root = v_position.xz + view.xz * (0.5 * v_height / shell_fall) - v_height * (0.5 * lean + 0.25 * bend);

    // The closest hit of the layers
    float hit_distance = 1e30;
    float hit_height = 0.0;         // over the ground
    float hit_part = 0.0;           // of its layer
    vec2 hit_point = vec2(0);       // of the blade that was met, in the tile of its layer
    vec2 hit_middle = vec2(0);      // where the layer was looked up, from which side and how (hitTexel)
    vec2 hit_heading = vec2(1, 0);
    vec2 hit_dx = vec2(0), hit_dy = vec2(0);
    float hit_push = 0.0, hit_dither = 0.0, hit_weight = 0.0;
    int hit_lower = 0;
    mat2 hit_turned = mat2(1.0);
    float hit_fall = 1.0;
    float baked = 0.0;
    float hit_lod = 0.0;
    vec3 hit_noise = vec3(0);
    float hit_tile = tile_size;
    float hit_layer_height = v_height;
    bool hit_undergrowth = false;
    int hit_kind = 0;

    // The plants of the place : small ones where the grass is short, in patches of each kind
    ivec3 kinds_here;
    vec3 kinds_keep;
    float under_keep;
    // The height of the grass is the same along straight lines between the vertices of the terrain : a noise
    // of a few metres winds the line where small and tall plants meet
    float tallness = (v_height / grass_height - (1.0 - patch_amount)) / max(patch_amount, 1e-3)
        + 0.25 * textureLod(u_noise, patch_root / 37.0 + vec2(0.71, 0.29), 0.0).y;
    species(patch_root, tallness, kinds_here, kinds_keep, under_keep);
    bool hit_bare = false;      // the plant that was met is not there : the ground of its layer
    int layers = layer_bits & 255;

    int count = layers + (undergrowth > 0.0 ? 1 : 0);
    for(int n = 0; n < count; n++) {

        bool is_undergrowth = n == layers;
        int i = is_undergrowth ? max_layers - 1 : n;
        // The undergrowth is short meadow grass under the tall kinds, nothing under the small plants.
        // keep : the part of the plants of the layer that are there (species)
        float keep = is_undergrowth ? under_keep : kinds_keep[min(n, 2)];
        if(keep <= 0.0) {
            continue;
        }
        int kind = is_undergrowth ? max(forced_kind, 0) : kinds_here[min(n, 2)];
        float tile = tile_size * kindTile(kind) * mix(1.0, layer_scales[i], is_undergrowth ? 1.0 : layer_spread);
        // No taller than the kind is, nor than the grass of the place
        float kind_top = min(grass_height * kindHeight(kind), v_height);
        // Each layer has its own patches, three in patch_size, under the height map of the shell
        // (grass.vert.glsl) : where one is short the others show
        float patch_noise = textureLod(u_noise, patch_root / (patch_size * 10.0 / 3.0) + vec2(0.17, 0.59) * float(i + 1), 0.0)[i % 3];
        float layer_height = kind_top * (1.0 - layer_patch * clamp(0.5 + 1.5 * patch_noise, 0.0, 1.0));
        if(is_undergrowth) {
            layer_height = kind_top * undergrowth * (1.0 - 0.5 * layer_patch * clamp(0.5 + 1.5 * patch_noise, 0.0, 1.0));
        }
        if(layer_height < 0.02 * grass_height) {
            continue;
        }
        ray_kind = kind;

        // A ray that would cross more than max_reach tiles of the layer is looked up as if it went down
        // faster : rays along the ground (and the back of the hills) still end somewhere close
        float fall = max(max(sink, length(view.xz + sink * slope) * layer_height / (tile * max_reach)), 1e-6);

        // The ray in root space, per unit of distance. reach : tiles crossed while going down the whole
        // layer, 1 for a baked ray
        vec2 flat_ray = view.xz + fall * slope;
        float flat_speed = length(flat_ray);
        vec2 along = flat_speed > 1e-6 ? flat_ray / flat_speed : vec2(1, 0);
        float reach = flat_speed * layer_height / (fall * tile);

        // Tiles it would cross before it was made to go down faster : which baked elevations it is
        // between. The lowest rays answer for the rays along the ground
        float flight = min(length(view.xz + sink * slope) * layer_height / (max(sink, 1e-6) * tile), 1e3);

        // Root of the point where the ray crosses the middle of the layer
        vec2 root = v_position.xz + view.xz * ((v_height - 0.5 * layer_height) / fall) - layer_height * (0.5 * lean + 0.25 * bend);

        // Each layer bends its tiling with its own noise, read at the root : it does not slide under
        // the blades with the view
        vec3 noise = textureLod(u_noise, root / tile_size * noise_frequency * warp_frequencies[i] + vec2(0.413, 0.271) * float(i), 0.0).xyz;
        vec2 warp = (i % 3 == 0 ? noise.xy : i % 3 == 1 ? noise.yz : noise.zx) * noise_amplitude;

        // The tile of the layer : turned and shifted
        float turn = float(i) * 2.39996;
        float c = cos(turn), s = sin(turn);
        mat2 turned = mat2(c, s, -s, c);
        vec2 tile_middle = turned * (root / tile) + vec2(0.37, 0.61) * float(i) + warp;
        vec2 heading = turned * along;
        float lod = lod_metre - log2(tile);

        vec2 dx = turned * pixel_long / tile, dy = turned * pixel_wide / tile;

        // Interleaved gradient noise, another one for each layer, and for the azimuth and the elevation
        vec2 dither = vec2(
            fract(52.9829189 * fract(dot(gl_FragCoord.xy + 17.0 * float(i), vec2(0.06711056, 0.00583715)))),
            fract(52.9829189 * fract(dot(gl_FragCoord.xy + 17.0 * float(i) + vec2(47.0, 113.0), vec2(0.06711056, 0.00583715)))));

        // One view per pixel close up, where blades are told apart (azimuth_dither)
        float push = azimuth_dither * (1.0 - smoothstep(0.5, 1.5, lod));

        float layer_hit, layer_baked, layer_ahead, layer_weight;
        int layer_lower;

        // The tangent is off by bend * (y - 0.5)² at the part y of the layer, nothing at the middle :
        // the ray is moved by that much for a first depth (from one baked view, that is enough), and
        // looked up there
        if(dot(bend, bend) > 1e-8) {
            lookup(tile_middle, heading, reach, flight, dx, dy, 1.0, dither, layer_hit, layer_baked, layer_ahead, layer_lower, layer_weight);
            float off = layer_hit - 0.5;
            tile_middle -= turned * bend * (layer_height * off * off / tile);
        }
        lookup(tile_middle, heading, reach, flight, dx, dy, push, dither, layer_hit, layer_baked, layer_ahead, layer_lower, layer_weight);

        // Towards the edge of its patch a kind loses its plants one by one, the last of the order of its
        // tile first : what is left stays spread evenly. A plant that is not there leaves the ground of
        // its layer, the other layers show behind it. Far away texels are means of plants : the kind that has more than half of its own
        bool bare = false;
        if(keep < 0.999) {
            float plant = hitPlant(tile_middle, heading, dx, dy, push, dither.x, layer_lower, layer_weight);
            if(mix(plant, 0.5, smoothstep(1.0, 3.0, lod)) >= keep) {
                bare = true;
                layer_hit = 0.0;
                layer_baked = 0.0;
                layer_ahead = 0.0;
            }
        }

        float layer_distance = (v_height - layer_hit * layer_height) / fall;
        if(layer_distance < hit_distance) {
            hit_distance = layer_distance;
            hit_height = layer_hit * layer_height;
            hit_part = layer_hit;
            hit_point = tile_middle + heading * layer_ahead;
            hit_middle = tile_middle;
            hit_heading = heading;
            hit_dx = dx;
            hit_dy = dy;
            hit_push = push;
            hit_dither = dither.x;
            hit_lower = layer_lower;
            hit_weight = layer_weight;
            hit_turned = turned;
            hit_fall = fall;
            baked = layer_baked;
            hit_lod = lod;
            hit_noise = noise;
            hit_tile = tile;
            hit_layer_height = layer_height;
            hit_undergrowth = is_undergrowth;
            hit_bare = bare;
            hit_kind = kind;
        }

    }

    // Where the ground falls away under the ray (a crest, the edge of a step) the ray does not go down
    // for ever : after l it is sink l - falling (flat_length l)² / 2 under the shell, `deepest` at most,
    // then it comes out of the grass again with the air behind it. What the lookup met deeper than the
    // ray ever gets is not there : nothing is drawn, whatever is behind shows between the blades.
    // falling : how much the ground bends down along the view, from the second derivatives of the
    // terrain density (marchingcubes.comp.glsl). Flat ground and hollows cut nothing : the ray does
    // meet grass further on. silhouette : more than 1 cuts higher than that (the real ray stays long
    // near its deepest point, and would meet more grass than the straight ray of the lookup : less than 1).
    // The bend is only known around the pixel : whether the ray does come out is not for this shader to
    // say. GrassPass gives a silhouette only to the pixels where the back of the shell is in front of
    // the ground, which is where it does; the others are drawn whole (docs/grass/13-see-through.md)
    float flat_length = length(view.xz);
    float falling = -(v_curve.x * view_flat.x * view_flat.x + 2.0 * v_curve.y * view_flat.x * view_flat.y + v_curve.z * view_flat.y * view_flat.y);
    if(silhouette > 0.0 && falling > 1e-5) {
        float deepest = sink * sink / (2.0 * falling * max(flat_length * flat_length, 1e-4));
        // The real ray stays long around its deepest point : it meets as much grass as the straight ray
        // of the lookup does `longer` times deeper (blades get thinner towards their tips). What the
        // lookup met on the ground of the tile is never there : the ray does not get down to it
        float longer = clamp(sqrt(16.0 / 3.0 * hit_fall / max(sink, 1e-4)), 1.0, 8.0);
        bool on_ground = deepest < v_height && (hit_distance >= 1e30 || hit_bare || hit_part < 0.06);
        if(on_ground || v_height - hit_height > deepest * longer / silhouette) {
            discard;
        }
    }

    // Normal and blade of the closest hit (every layer may be too short to be there)
    vec3 tile_normal = vec3(0, 1, 0);
    float hit_id = 0.5;
    vec3 hit_colour = root_color;
    ray_kind = hit_kind;
    if(hit_distance < 1e30 && !hit_bare) {
        vec3 texel = hitTexel(hit_middle, hit_heading, hit_dx, hit_dy, hit_push, hit_dither, hit_lower, hit_weight, hit_colour);
        vec3 stored_normal = decodeNormal(texel.xy);
        tile_normal = vec3(stored_normal.xz * hit_turned, stored_normal.y).xzy;
        hit_id = texel.z;
    }

    // Patches of soil (terrain.frag.glsl) : no blade is rooted in one. Over the edge the blades go one
    // by one, by their id : tufts further and further apart, then the soil. Far away blades are not
    // told apart : one line
    // The same where the ground gets steep, by the slope where the blade is rooted
    float rooted = hit_part * hit_layer_height;
    vec2 hit_root = v_position.xz + view.xz * ((v_height - rooted) / hit_fall) - rooted * (lean + bend * hit_part);
    float soil_grass = slopeGrass(hit_root, ground_normal.y);
    if(chunk.w >= 0.0) {
        vec3 soil = soilAt(hit_root + soilWarp(hit_root), chunk.w, map_scale).rgb;
        soil_grass = min(soil_grass, 1.0 - smoothstep(0.4, 0.6, max(soil.r, max(soil.g, soil.b))));
    }
    if(soil_grass < 1.0 && mix(hit_id, 0.5, smoothstep(1.0, 3.0, hit_lod)) >= soil_grass) {
        discard;
    }

    // Between the blades : dark ground, kept over the terrain (which the flat ground of the tile only
    // follows roughly, the terrain would show in the hollows)
    hit_height = max(hit_height, 0.12 * v_height);

    // Where the grass ends its top is the shell : nothing to see through next to the rock
    hit_height = mix(v_height, hit_height, smoothstep(0.0, 0.6, v_height / grass_height));

    // Tile space to world : x and z of the tile on the ground, y along the blades
    vec3 axis_x = vec3(hit_tile, -hit_tile * ground_normal.x / ground_normal.y, 0);
    vec3 axis_z = vec3(0, -hit_tile * ground_normal.z / ground_normal.y, hit_tile);
    vec2 blade_slope = lean + 2.0 * bend * hit_part;
    vec3 axis_y = hit_layer_height * vec3(blade_slope.x, ground_normal.y * growth.y / growth_up, blade_slope.y);
    vec3 normal = normalize(cross(axis_y, axis_z) * tile_normal.x + cross(axis_z, axis_x) * tile_normal.y + cross(axis_x, axis_y) * tile_normal.z);
    if(dot(normal, view) > 0.0) {
        normal = -normal;
    }

    // The colour the rays met (logic/grass/grass_model.h : green by the height, flowers, seeds), or green
    // by the height alone. Each blade has its own : lighter or darker, a little yellow or blue
    vec3 color = mix(mix(root_color, tip_color, baked), hit_colour, baked_colour) * (1.0 + 0.3 * hit_noise.z);
    float hue = fract(hit_id * 7.3) - 0.5;
    color *= (1.0 + colour_variation * (hit_id - 0.5)) * (1.0 + colour_variation * hue * vec3(0.5, 0.1, -0.6));

    // Dry blades, in patches : those whose id is under what the noise allows there. Far away the
    // texels are means of blades (id 0.5) : the same part of the colour instead
    float dry_part = dryness * 1.5 * clamp(0.5 + 1.5 * hit_noise.y, 0.0, 1.0);
    dry_part = mix(dry_part, 1.0, 0.7 * (1.0 - soil_grass));    // the last ones before the soil are dry
    // (the id is the rank of the plant in its tile, which the species and the soil thin by : another order here)
    float dry = mix(smoothstep(0.0, 0.06, dry_part - fract(hit_id * 5.3 + 0.37)), dry_part, smoothstep(1.0, 3.0, hit_lod));
    color = mix(color, mix(dry_root_color, dry_tip_color, baked), dry);

    // The sun ray through the blade that was met, in the tile space of its layer (far away the
    // texels are means : no shadow to find)
    float lit = 1.0;
    if(shadow > 0.0 && hit_lod < 4.0) {
        float sun_fall = dot(sun_direction, ground_normal) / ground_normal.y;
        vec2 sun_ray = -sun_direction.xz + sun_fall * blade_slope;
        float sun_speed = length(sun_ray);
        vec2 sun_heading = hit_turned * (sun_speed > 1e-6 ? sun_ray / sun_speed : vec2(1, 0));
        float sun_reach = sun_speed * hit_layer_height / (max(sun_fall, 1e-3) * hit_tile);
        float hidden = sunShadow(hit_point, baked, sun_heading, sun_reach, hit_lod);
        lit = 1.0 - shadow * hidden * (1.0 - smoothstep(2.0, 4.0, hit_lod));
    }

    // Darker deep in the grass, light goes through the blades
    float facing = dot(normal, sun_direction);
    float light = (max(facing, 0.0) + 0.6 * max(-facing, 0.0)) * lit;
    float occlusion = mix(0.35, 1.0, baked);

    // Sky light : less under the canopy, which the top rays give over the blade that was met. A blade in
    // the open stays light down to the ground, the heart of a tuft is dark. The height keeps a part :
    // one column does not know the tufts around
    if(canopy > 0.0) {
        float under = max(textureLod(u_top, vec3(hit_point, ray_kind), hit_lod).r - baked, 0.0);
        occlusion = mix(occlusion, mix(0.3, 1.0, exp(-3.0 * under)) * mix(0.6, 1.0, baked), canopy);
    }

    // The undergrowth is under the tall layers too
    if(hit_undergrowth) {
        occlusion *= mix(0.75, 1.0, hit_layer_height / v_height);
    }
    // Under the plants (the shell is close enough to the ground)
    float shade = treeShade(v_position, 0.0);
    vec3 unlit = color * occlusion * (ambientLight(v_position, ground_normal, shade) + pointLights(v_position, normalize(ground_normal + normal), 0.5));
    vec3 sunlit = color * light_color.rgb * (light * occlusion);
    sunlit += vec3(0.9, 1.0, 0.7) * light_color.rgb * (0.17 * occlusion * lit * pow(max(dot(reflect(-sun_direction, normal), -view), 0.0), 24.0));

    writeLit(unlit, sunlit, shade, v_position, liftAlpha(0.3 + terminatorLift(dFdx(v_position), dFdy(v_position), ground_normal, 4.0)));

    // Depth of the hit. The smooth normal says roughly where the ground is : stay over the triangle
    // that is really under this pixel
    hit_distance = (v_height - hit_height) / hit_fall;
    vec3 ground_x = dFdx(v_position) - vec3(0, dFdx(v_height), 0), ground_y = dFdy(v_position) - vec3(0, dFdy(v_height), 0);
    vec3 flat_normal = cross(ground_x, ground_y);
    flat_normal *= sign(flat_normal.y);
    float flat_descent = dot(view, flat_normal) / max(flat_normal.y, 1e-12);
    if(flat_descent < 0.0) {
        hit_distance = min(hit_distance, 0.97 * v_height / -flat_descent);
    }
    vec4 clip = viewproj * vec4(v_position + view * hit_distance, 1.0);
    gl_FragDepth = clamp(max(clip.z / clip.w, gl_FragCoord.z), 0.0, 1.0);

}
