// Reading the irradiance probes (renderer/lighting/probe_volume.h, logic/lighting/probes.h). Only with
// RAY_TRACING, after view.glsl; needs GL_EXT_nonuniform_qualifier.
//
// A chunk with a surface has a brick of 65³ probes, one every 4 m, in a page of 8 bricks. The brick of a
// position of the world is found through a small table of chunk coordinates that wraps

// Brick + 1 of the chunk at each cell, 0 : none
layout(set = 0, binding = 7) uniform usampler3D u_probe_index;
// Three textures per page : the light arriving at each probe in red, green and blue. x : its mean,
// yzw : twice the mean of light times direction (ProbeLight)
layout(set = 0, binding = 8) uniform sampler3D u_probe_light[];
// One per page. rgb : how much of the sun (or of the moon) reaches the probe, tinted by the leaves it
// crossed, a : how much of the sky the probe sees
layout(set = 0, binding = 9) uniform sampler3D u_probe_direct[];

const float probe_chunk = 256.0;        // metres a side of a chunk
const float probe_spacing = 4.0;
const float probe_brick = 65.0;         // probes a side of a brick
const float probe_page = 130.0;         // texels a side of a page

// Where the probes around a position are : the page and the coordinates in its textures. false : the chunk
// has no brick
bool probeAt(vec3 position, out int page, out vec3 at) {
    vec3 chunk = floor(position / probe_chunk);
    uint brick = texelFetch(u_probe_index, ivec3(chunk) & 15, 0).r;
    if(brick == 0u) return false;
    brick -= 1u;
    page = int(brick >> 3);
    uint slot = brick & 7u;
    // Texel centres are on the probes : filtering between them never leaves the brick
    vec3 texel = (position - chunk * probe_chunk) / probe_spacing + 0.5 + vec3(slot & 1u, (slot >> 1) & 1u, slot >> 2) * probe_brick;
    at = texel / probe_page;
    return true;
}

// Light of the sky and of what bounces on a surface : what a white surface of that normal shows.
// Read a metre off the surface, less light comes through thin walls. open : without a brick
vec3 probeLight(vec3 position, vec3 normal, vec3 open) {
    int page;
    vec3 at;
    if(!probeAt(position + normal, page, at)) return open;
    vec4 n = vec4(1.0, normal);
    return max(vec3(
        dot(textureLod(u_probe_light[nonuniformEXT(page * 3)], at, 0.0), n),
        dot(textureLod(u_probe_light[nonuniformEXT(page * 3 + 1)], at, 0.0), n),
        dot(textureLod(u_probe_light[nonuniformEXT(page * 3 + 2)], at, 0.0), n)), vec3(0.0));
}

// rgb : how much of the sun reaches the probes around a position, a : how much of the sky they see.
// All of both without a brick
vec4 probeDirect(vec3 position) {
    int page;
    vec3 at;
    if(!probeAt(position, page, at)) return vec4(1.0);
    return textureLod(u_probe_direct[nonuniformEXT(page)], at, 0.0);
}
