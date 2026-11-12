#version 450

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_normal;  // octahedral (marchingcubes.comp.glsl)
layout(location = 2) in vec3 a_curve;   // how the ground bends : second derivatives of its height, xx, xz, zz

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out float v_height;
layout(location = 3) out vec3 v_curve;
layout(location = 4) out float v_cover;     // 1 where the grass grows whole, less where the slope or the soil ends it

layout(push_constant) uniform Args {
    vec4 base_normal;
    vec4 chunk;         // xy : origin of the chunk on the ground, z : layer of its height map, w : of its soil map (< 0 : none)
    float map_scale;    // texels of those maps per metre
    float tile_size;
    float grass_height;
    float patch_size;
    float map_top;      // what the height map holds at its highest, as a part of grass_height
    // the fragment shader has more
};

// The top of the plants, one layer per chunk : the tallest of the kinds that grow at each terrain vertex,
// each as tall as its own noise says (logic/grass/grass_species.h, GrassTopField). Texel (i, j) is on the
// vertices i, j cubes from the origin of the chunk
layout(set = 1, binding = 3) uniform sampler2DArray u_height_map;

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos; // w : time
};

// Mud, stones and sand over each chunk, on the same vertices, and in a how tall the grass is next to
// them (renderer/terrain/soil_maps.h)
layout(set = 0, binding = 2) uniform sampler2DArray u_soil;

vec3 decodeNormal(vec2 e) {
    vec3 n = vec3(e.x, 1.0 - abs(e.x) - abs(e.y), e.y);
    float fold = max(-n.y, 0.0);
    n.x += n.x >= 0.0 ? -fold : fold;
    n.z += n.z >= 0.0 ? -fold : fold;
    return normalize(n);
}

// How tall the grass grows on a slope, by how far up its normal points : whole on level ground, a quarter
// where the last blades are (slopeGrass ends them). The same in grass.frag.glsl
float slopeTall(float up) {
    return mix(0.25, 1.0, smoothstep(0.74, 0.88, up));
}

void main() {

    vec3 normal = decodeNormal(a_normal);

    // Only on flat ground, slopes keep the rock of the terrain pass : the grass gets shorter as the
    // ground gets steeper, and the shell comes down to the ground with it. How short is worked out per
    // pixel (slopeTall in grass.frag.glsl, on a normal a noise moves by 0.05) : the shell only has to
    // stay over that, so it is the same curve a little further up the slope. Its own height goes
    // straight from a vertex to the next : grass as tall as the shell showed the triangles of the terrain
    v_cover = slopeTall(normal.y + 0.08) * smoothstep(0.62, 0.7, normal.y);

    // Shorter towards the patches of soil, none well inside them (soil::grassHeight). The edge itself
    // is cut blade by blade in the fragment shader, a few metres to either side of where the map has it :
    // the shell goes on that far into the patch
    if(chunk.w >= 0.0) {
        vec2 soil_texel = ((a_pos.xz - chunk.xy) * map_scale + 1.5) / vec2(textureSize(u_soil, 0).xy);    // a border of one texel
        v_cover *= textureLod(u_soil, vec3(soil_texel, chunk.w), 0.0).a;
    }

    // As high as the plants of the place, from the height map of the chunk : low over clover, high over
    // tall grass. Here and not per pixel : the shell stays at the top of the plants
    vec2 texel = ((a_pos.xz - chunk.xy) * map_scale + 0.5) / vec2(textureSize(u_height_map, 0).xy);
    v_height = grass_height * map_top * textureLod(u_height_map, vec3(texel, chunk.z), 0.0).r * v_cover;

    // Grass shell : the terrain surface at the top of the grass, straight above the ground
    vec3 position = a_pos + vec3(0, v_height, 0);
    gl_Position = viewproj * vec4(position, 1.0);
    v_position = position;
    v_normal = normal;
    v_curve = a_curve;

}
