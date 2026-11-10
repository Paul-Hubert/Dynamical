#version 450

// Grass without blade geometry. The pixel is on a shell at the top of the grass; its view ray is looked up
// in rays baked against a tile of grass blades (logic/grass/grass_bake.h, mirrored on the CPU by lookupGrass) :
//  - u_oblique : rays going down at 45 degrees, for a position in the tile and an azimuth
//  - u_top : rays going straight down
// r is the height of the hit (0 ground, 1 top of the grass), g b its normal (octahedral, decodeNormal),
// a a random value per blade (0.5 on the ground).
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

layout(location = 0) out vec4 outColor;

// The hit is never in front of the shell : keeps the early depth test
layout(depth_greater) out float gl_FragDepth;

layout(push_constant) uniform Args {
    vec4 base_normal;       // xyz : direction the blades grow in, w : how much they follow the terrain normal
    float tile_size;
    float grass_height;
    float patch_size;
    float patch_amount;
    float noise_frequency;
    float noise_amplitude;
    float wind_strength;
    float wind_speed;
    float top_start;
    int layers;
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
};

layout(set = 1, binding = 0) uniform sampler3D u_oblique;
layout(set = 1, binding = 1) uniform sampler2D u_top;
layout(set = 1, binding = 2) uniform sampler2D u_noise;

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos; // w : time
    vec4 tree_shade; // x : strength, y : 1 / width of the map in metres, z : distance where it ends
};

// Shadows of the crowns of the plants on the ground, seen from above (renderer/tree_shade.h)
layout(set = 0, binding = 1) uniform sampler2D u_tree_shade;

const float pi = 3.14159265359;

// Rays closer to the horizontal are looked up as if they crossed this many tiles (grass_max_reach)
const float max_reach = 3.0;

// Up to three tall layers, and the undergrowth in the last place : short and dense.
// Tiles of different sizes only repeat together very far
// Mips are means of blades : thin ones fade before they are too small to see. Sharper, a little aliased
const float mip_bias = -0.8;

const int max_layers = 4;
const float layer_scales[max_layers] = float[](1.0, 1.37, 1.73, 0.55);
const float warp_frequencies[max_layers] = float[](1.0, 1.37, 0.73, 1.61);

const vec3 sun_direction = normalize(vec3(0.5, 1.0, 0.5));
const vec3 sky_color = vec3(0.45, 0.6, 0.8);

float shadeHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float shadeNoise(vec2 p) {
    vec2 cell = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(shadeHash(cell), shadeHash(cell + vec2(1, 0)), f.x),
               mix(shadeHash(cell + vec2(0, 1)), shadeHash(cell + vec2(1, 1)), f.x), f.y);
}

// How much of the sun the plants hide from a point `above` its ground : 0 in the open, tree_shade.x under
// thick crowns. The map only knows how thick the crowns are : a noise lets flecks of sun through them
float treeShade(vec3 position, float above) {
    if(tree_shade.x <= 0.0) return 0.0;
    float far = length(position - viewpos.xyz);
    if(far >= tree_shade.z) return 0.0;
    vec2 ground = position.xz - sun_direction.xz / sun_direction.y * above;
    float depth = texture(u_tree_shade, ground * tree_shade.y).r * 4.0;
    float flecks = mix(0.6 * shadeNoise(ground * 0.7) + 0.4 * shadeNoise(ground * 2.1 + 17.0), 0.5, smoothstep(25.0, 80.0, far));
    float shade = 1.0 - exp(-depth * (0.2 + 1.6 * flecks));
    return tree_shade.x * shade * (1.0 - smoothstep(0.75 * tree_shade.z, tree_shade.z, far));
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

// One layer : the baked ray through the same point of the middle of the grass, from the same side.
// hit : height of the hit on the view ray, baked : height read (how deep in the grass the blade was met),
// texel : normal as stored and blade id, ahead : how far after the middle the blade is, in tiles.
// dither : 0..1, fixed for the pixel
void lookup(vec2 tile_middle, vec2 heading, float reach, float lod, float dither, out float hit, out float baked, out vec3 texel, out float ahead) {

    // The sampler blends the two nearest baked azimuths, which shows a thin blade twice. Close up each
    // pixel takes one of the two instead, more often the nearer one : the same blend over neighbouring
    // pixels, one sharp view in each
    float azimuth = atan(heading.y, heading.x) / (2.0 * pi);
    float slices = float(textureSize(u_oblique, 0).z);
    float slice = azimuth * slices - 0.5;
    float one = (floor(slice) + (fract(slice) > dither ? 1.0 : 0.0) + 0.5) / slices;
    azimuth = mix(azimuth, one, azimuth_dither * (1.0 - smoothstep(0.5, 1.5, lod)));

    vec4 oblique = textureLod(u_oblique, vec3(tile_middle, azimuth), lod);

    // The baked ray met a blade (0.5 - r) tiles further than the middle. Blades are mostly vertical :
    // the view ray meets it at the same place, higher or lower. The baked hit itself at 45 degrees
    hit = clamp(0.5 - (0.5 - oblique.r) / max(reach, 1e-3), 0.0, 1.0);
    baked = oblique.r;
    texel = oblique.gba;
    ahead = 0.5 - oblique.r;

    // Between 45 degrees and the vertical : towards the rays from the top
    if(reach < 1.0) {
        float steep = clamp((atan(1.0, reach) - pi / 4.0) / (pi / 4.0), 0.0, 1.0);
        float top_weight = smoothstep(min(top_start, 0.999), 1.0, steep);
        vec4 top = textureLod(u_top, tile_middle, lod);
        hit = mix(hit, top.r, top_weight);
        baked = mix(baked, top.r, top_weight);
        texel = mix(texel, top.gba, top_weight);
        ahead *= 1.0 - top_weight;
    }

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
    float baked = textureLod(u_oblique, vec3(middle, atan(heading.y, heading.x) / (2.0 * pi)), lod).r;
    float seen = clamp(0.5 - (0.5 - baked) / slanted_reach, 0.0, 1.0);
    float slanted = smoothstep(0.08, 0.3, seen - height);

    float above = 0.0;
    for(int k = 1; k <= 2; k++) {
        float y = mix(height, 1.0, float(k) / 2.5);
        float canopy = textureLod(u_top, point - heading * reach * (y - height), lod).r;
        above = max(above, smoothstep(0.05, 0.25, canopy - y));
    }

    return mix(slanted, above, steep);

}

void main() {

    // No grass on the slopes : the shell comes down to the ground there (grass.vert.glsl), and the edge
    // follows the normal like the rock of the terrain pass does
    vec3 ground_normal = normalize(v_normal);
    if(v_height < 0.03 * grass_height || ground_normal.y < 0.74) {
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
    float pixel_size = view_distance * pixel_angle / sqrt(max(-dot(view, ground_normal), 0.05));
    float lod_metre = log2(max(pixel_size * float(textureSize(u_oblique, 0).x), 1e-6)) + mip_bias;

    // Where the height patches of the layers are read : the root of the ray at the middle of the shell,
    // the same for every layer and close to the same from every view
    float shell_fall = max(max(sink, length(view.xz + sink * slope) * v_height / (tile_size * max_reach)), 1e-6);
    vec2 patch_root = v_position.xz + view.xz * (0.5 * v_height / shell_fall) - v_height * (0.5 * lean + 0.25 * bend);

    // The closest hit of the layers
    float hit_distance = 1e30;
    float hit_height = 0.0;         // over the ground
    float hit_part = 0.0;           // of its layer
    vec2 hit_point = vec2(0);       // of the blade that was met, in the tile of its layer
    mat2 hit_turned = mat2(1.0);
    float hit_fall = 1.0;
    float baked = 0.0;
    vec3 tile_normal = vec3(0, 1, 0);
    float hit_id = 0.5;
    float hit_lod = 0.0;
    vec3 hit_noise = vec3(0);
    float hit_tile = tile_size;
    float hit_layer_height = v_height;
    bool hit_undergrowth = false;

    int count = layers + (undergrowth > 0.0 ? 1 : 0);
    for(int n = 0; n < count; n++) {

        bool is_undergrowth = n == layers;
        int i = is_undergrowth ? max_layers - 1 : n;
        float tile = tile_size * mix(1.0, layer_scales[i], is_undergrowth ? 1.0 : layer_spread);
        // Each layer has its own patches, three in a patch of the field (grass.vert.glsl) : where one
        // is short the others show
        float patch_noise = textureLod(u_noise, patch_root / (patch_size * 10.0 / 3.0) + vec2(0.17, 0.59) * float(i + 1), 0.0)[i % 3];
        float layer_height = v_height * (1.0 - layer_patch * clamp(0.5 + 1.5 * patch_noise, 0.0, 1.0));
        if(is_undergrowth) {
            layer_height = v_height * undergrowth * (1.0 - 0.5 * layer_patch * clamp(0.5 + 1.5 * patch_noise, 0.0, 1.0));
        }
        if(layer_height < 0.05 * v_height) {
            continue;
        }

        // A ray that would cross more than max_reach tiles of the layer is looked up as if it went down
        // faster : rays along the ground (and the back of the hills) still end somewhere close
        float fall = max(max(sink, length(view.xz + sink * slope) * layer_height / (tile * max_reach)), 1e-6);

        // The ray in root space, per unit of distance. reach : tiles crossed while going down the whole
        // layer, 1 for a baked ray
        vec2 flat_ray = view.xz + fall * slope;
        float flat_speed = length(flat_ray);
        vec2 along = flat_speed > 1e-6 ? flat_ray / flat_speed : vec2(1, 0);
        float reach = flat_speed * layer_height / (fall * tile);

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

        // Interleaved gradient noise, another one for each layer
        float dither = fract(52.9829189 * fract(dot(gl_FragCoord.xy + 17.0 * float(i), vec2(0.06711056, 0.00583715))));

        float layer_hit, layer_baked, layer_ahead;
        vec3 layer_texel;
        lookup(tile_middle, heading, reach, lod, dither, layer_hit, layer_baked, layer_texel, layer_ahead);

        // The tangent is off by bend * (y - 0.5)² at the part y of the layer, nothing at the middle :
        // the ray is moved by that much for the depth just found, and looked up again
        if(dot(bend, bend) > 1e-8) {
            float off = layer_hit - 0.5;
            tile_middle -= turned * bend * (layer_height * off * off / tile);
            lookup(tile_middle, heading, reach, lod, dither, layer_hit, layer_baked, layer_texel, layer_ahead);
        }

        float layer_distance = (v_height - layer_hit * layer_height) / fall;
        if(layer_distance < hit_distance) {
            hit_distance = layer_distance;
            hit_height = layer_hit * layer_height;
            hit_part = layer_hit;
            hit_point = tile_middle + heading * layer_ahead;
            hit_turned = turned;
            hit_fall = fall;
            baked = layer_baked;
            vec3 layer_normal = decodeNormal(layer_texel.xy);
            tile_normal = vec3(layer_normal.xz * turned, layer_normal.y).xzy;
            hit_id = layer_texel.z;
            hit_lod = lod;
            hit_noise = noise;
            hit_tile = tile;
            hit_layer_height = layer_height;
            hit_undergrowth = is_undergrowth;
        }

    }

    // On a crest the ground is seen edge on, and a ray that meets nothing high in its first tile goes
    // on over the crest : only the tall parts of the blades stay there, tips against what is behind.
    // Only there, and only while blades can be told apart : on flat ground the ray does meet grass
    // further on, which one lookup cannot know
    float edge = 1.0 - smoothstep(0.03, 0.14, -dot(view, ground_normal));
    edge *= 1.0 - smoothstep(0.5, 2.0, hit_lod);
    if(baked < edge * silhouette) {
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

    // Each blade has its own green : lighter or darker, a little yellow or blue
    vec3 color = mix(root_color, tip_color, baked) * (1.0 + 0.3 * hit_noise.z);
    float hue = fract(hit_id * 7.3) - 0.5;
    color *= (1.0 + colour_variation * (hit_id - 0.5)) * (1.0 + colour_variation * hue * vec3(0.5, 0.1, -0.6));

    // Dry blades, in patches : those whose id is under what the noise allows there. Far away the
    // texels are means of blades (id 0.5) : the same part of the colour instead
    float dry_part = dryness * 1.5 * clamp(0.5 + 1.5 * hit_noise.y, 0.0, 1.0);
    float dry = mix(smoothstep(0.0, 0.06, dry_part - hit_id), dry_part, smoothstep(1.0, 3.0, hit_lod));
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
        float under = max(textureLod(u_top, hit_point, hit_lod).r - baked, 0.0);
        occlusion = mix(occlusion, mix(0.3, 1.0, exp(-3.0 * under)) * mix(0.6, 1.0, baked), canopy);
    }

    // The undergrowth is under the tall layers too
    if(hit_undergrowth) {
        occlusion *= mix(0.75, 1.0, hit_layer_height / v_height);
    }
    // Under the plants (the shell is close enough to the ground)
    float shade = treeShade(v_position, 0.0);
    lit *= 1.0 - shade;
    color *= (light * (1.0 - shade) * 0.7 + 0.3 * (1.0 - 0.35 * shade)) * occlusion;
    color += vec3(0.9, 1.0, 0.7) * 0.12 * occlusion * lit * pow(max(dot(reflect(-sun_direction, normal), -view), 0.0), 24.0);

    float fog = 1.0 - exp(-view_distance * 0.0006);
    outColor = vec4(mix(color, sky_color, fog), 1.0);

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
