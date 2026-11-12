#version 460

// The shell of the grass seen from behind (GrassPass, docs/grass/13-see-through.md) : nothing is drawn,
// the pixel is marked in the stencil where that face is in front of the ground. A view ray that entered
// the grass in front of it comes out of it there, over a crest, with the air behind : the only place
// where blades are seen through (grass.frag.glsl). Elsewhere the ray ends on the ground, in the grass

layout(location = 2) in float v_height;

layout(push_constant) uniform Args {
    vec4 base_normal;
    vec4 chunk;
    float map_scale;
    float tile_size;
    float grass_height;
    // grass.frag.glsl has more
};

void main() {

    // No grass : the shell is the ground itself
    if(v_height < 0.03 * grass_height) {
        discard;
    }

}
