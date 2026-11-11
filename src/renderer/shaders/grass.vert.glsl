#version 450

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_normal;  // octahedral (marchingcubes.comp.glsl)
layout(location = 2) in vec3 a_curve;   // how the ground bends : second derivatives of its height, xx, xz, zz

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out float v_height;
layout(location = 3) out vec3 v_curve;

layout(push_constant) uniform Args {
    vec4 base_normal;
    vec4 chunk;         // xy : origin of the chunk on the ground, z : layer of its height map, w : of its soil map (< 0 : none)
    float map_scale;    // texels of those maps per metre
    float tile_size;
    float grass_height;
    float patch_size;
    float patch_amount;
    // the fragment shader has more
};

// How tall the grass is, one layer per chunk : octaves of Perlin noise baked on the terrain vertices
// (logic/grass/grass_height.h). Texel (i, j) is on the vertices i, j cubes from the origin of the chunk
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

void main() {

    vec3 normal = decodeNormal(a_normal);

    // Only on flat ground, slopes keep the rock of the terrain pass : the grass gets shorter towards them
    v_height = grass_height * smoothstep(0.68, 0.86, normal.y);

    // Taller and shorter grass, from the height map of the chunk.
    // Here and not per pixel : the shell stays at the top of the grass
    vec2 texel = ((a_pos.xz - chunk.xy) * map_scale + 0.5) / vec2(textureSize(u_height_map, 0).xy);
    float tall = textureLod(u_height_map, vec3(texel, chunk.z), 0.0).r;
    v_height *= 1.0 - patch_amount * (1.0 - tall);

    // Shorter towards the patches of soil, none well inside them (soil::grassHeight). The edge itself
    // is cut blade by blade in the fragment shader, a few metres to either side of where the map has it :
    // the shell goes on that far into the patch
    if(chunk.w >= 0.0) {
        vec2 soil_texel = ((a_pos.xz - chunk.xy) * map_scale + 1.5) / vec2(textureSize(u_soil, 0).xy);    // a border of one texel
        v_height *= textureLod(u_soil, vec3(soil_texel, chunk.w), 0.0).a;
    }

    // Grass shell : the terrain surface at the top of the grass, straight above the ground
    vec3 position = a_pos + vec3(0, v_height, 0);
    gl_Position = viewproj * vec4(position, 1.0);
    v_position = position;
    v_normal = normal;
    v_curve = a_curve;

}
