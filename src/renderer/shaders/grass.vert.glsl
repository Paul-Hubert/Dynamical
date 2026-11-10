#version 450

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out float v_height;

layout(push_constant) uniform Args {
    vec4 base_normal;
    float tile_size;
    float grass_height;
    float patch_size;
    float patch_amount;
    // the fragment shader has more
};

layout(set = 1, binding = 2) uniform sampler2D u_noise;

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos; // w : time
};

void main() {

    // Only on flat ground, slopes keep the rock of the terrain pass : the grass gets shorter towards them
    v_height = grass_height * smoothstep(0.68, 0.86, normalize(a_normal).y);

    // Patches of taller and shorter grass, one every patch_size (the noise has 10 features across).
    // Here and not per pixel : the shell stays at the top of the grass
    float patch_noise = textureLod(u_noise, a_pos.xz / (patch_size * 10.0), 0.0).x;
    v_height *= 1.0 - patch_amount * clamp(0.5 + 1.5 * patch_noise, 0.0, 1.0);

    // Grass shell : the terrain surface at the top of the grass, straight above the ground
    vec3 position = a_pos + vec3(0, v_height, 0);
    gl_Position = viewproj * vec4(position, 1.0);
    v_position = position;
    v_normal = a_normal;

}
