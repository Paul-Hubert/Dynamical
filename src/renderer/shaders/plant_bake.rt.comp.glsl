#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

// The volume of a species (renderer/lighting/plant_volumes.h, docs/lighting.md) : what the plant is far
// away, in the rays and on screen. Baked once, against the acceleration structure of its most detailed
// level, placed on its own in the scene of the frame (RayBake). An invocation a texel :
//  - short rays in all directions from points of the texel, with the alpha test of the leaves : what they
//    let through gives how much light the texel stops, how many meet something how likely a ray is to
//    stop there, and what they meet first its mean colour and normal
//  - rays to the sky through the whole plant : how much of it the texel sees

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

#include "include/view.glsl"
#include "include/rays.glsl"

layout(set = 1, binding = 0, rgba8) uniform writeonly image3D u_density;
layout(set = 1, binding = 1, rgba8) uniform writeonly image3D u_color;
layout(set = 1, binding = 2, rgba8) uniform writeonly image3D u_normal;

layout(push_constant) uniform Args {
    vec4 origin;        // xyz : where the plant stands for the bake, w : a texel in metres
    vec4 low;           // xyz : the corner of the volume, in the space of the plant
    ivec4 size;         // xyz : texels of the volume, w : first slice of this dispatch
    vec4 heart;         // xyz : a point inside the plant its normals turn away from, w : slices of this dispatch
    vec4 fill;          // rgb : colour of the texels where nothing is (filtering reads them), a : leaf
};

const int short_rays = 24;
const int sky_rays = 8;

uint hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float random(inout uint state) {
    state = hash(state);
    return float(state) * (1.0 / 4294967296.0);
}

vec3 fibonacci(int i, int n) {
    float y = 1.0 - (2.0 * float(i) + 1.0) / float(n);
    float radius = sqrt(max(1.0 - y * y, 0.0));
    float angle = 2.39996323 * float(i);
    return vec3(radius * cos(angle), y, radius * sin(angle));
}

void main() {

    ivec3 texel = ivec3(gl_GlobalInvocationID) + ivec3(0, 0, size.w);
    if(any(greaterThanEqual(texel, size.xyz)) || int(gl_GlobalInvocationID.z) >= int(heart.w)) return;

    float metres = origin.w;
    vec3 centre = low.xyz + (vec3(texel) + 0.5) * metres;
    uint state = hash(uint(texel.x) + 97u * uint(texel.y) + 9973u * uint(texel.z) + 13u);

    vec3 let_through = vec3(0.0);
    float met = 0.0, leaves = 0.0;
    vec3 colour = vec3(0.0), normal = vec3(0.0);

    for(int i = 0; i < short_rays; i++) {
        vec3 local = centre + (vec3(random(state), random(state), random(state)) - 0.5) * metres;
        vec3 direction = fibonacci(i, short_rays);
        // The directions turn from texel to texel
        direction = direction.zxy * (random(state) < 0.5 ? 1.0 : -1.0);

        vec3 light = vec3(1.0);
        float nearest = 1e9;
        vec3 nearest_colour = vec3(0.0), nearest_normal = vec3(0.0);
        float nearest_leaf = 0.0;

        rayQueryEXT query;
        rayQueryInitializeEXT(query, u_scene, 0u, ray_bake, origin.xyz + local, 0.0, direction, metres);
        while(rayQueryProceedEXT(query)) {
            if(rayQueryGetIntersectionTypeEXT(query, false) != gl_RayQueryCandidateIntersectionTriangleEXT) continue;
            int index = rayQueryGetIntersectionInstanceCustomIndexEXT(query, false) + rayQueryGetIntersectionGeometryIndexEXT(query, false);
            RayGeometry geometry = ray_geometries[index];
            int primitive = rayQueryGetIntersectionPrimitiveIndexEXT(query, false);
            if(!rayLeaf(geometry, primitive, rayQueryGetIntersectionBarycentricsEXT(query, false))) continue;
            light *= geometry.transmission;
            float t = rayQueryGetIntersectionTEXT(query, false);
            if(t < nearest) {
                nearest = t;
                nearest_colour = geometry.color;
                uvec3 corners = rayCorners(geometry, primitive);
                vec3 a = rayPosition(geometry, corners.x);
                nearest_normal = cross(rayPosition(geometry, corners.y) - a, rayPosition(geometry, corners.z) - a);
                nearest_leaf = 1.0;
            }
        }
        if(rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
            // Wood : nothing goes through
            light = vec3(0.0);
            float t = rayQueryGetIntersectionTEXT(query, true);
            if(t < nearest) {
                nearest = t;
                int index = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true) + rayQueryGetIntersectionGeometryIndexEXT(query, true);
                RayGeometry geometry = ray_geometries[index];
                nearest_colour = geometry.color;
                uvec3 corners = rayCorners(geometry, rayQueryGetIntersectionPrimitiveIndexEXT(query, true));
                vec3 a = rayPosition(geometry, corners.x);
                nearest_normal = cross(rayPosition(geometry, corners.y) - a, rayPosition(geometry, corners.z) - a);
                nearest_leaf = 0.0;
            }
        }

        let_through += light;
        if(nearest < 1e8) {
            met += 1.0;
            leaves += nearest_leaf;
            colour += nearest_colour;
            // Leaves have two sides : the one that looks out of the plant
            float area = length(nearest_normal);
            if(area > 0.0) {
                nearest_normal /= area;
                vec3 out_of = local + direction * nearest - heart.xyz;
                normal += dot(nearest_normal, out_of) < 0.0 ? -nearest_normal : nearest_normal;
            }
        }
    }

    // How much of the sky over it the texel sees through its own plant
    float sky = 0.0;
    for(int i = 0; i < sky_rays; i++) {
        vec3 direction = fibonacci(i, 2 * sky_rays);
        vec3 light = traceShadow(origin.xyz + centre, normalize(direction + vec3(0.0, 0.15, 0.0)), 200.0, ray_bake);
        sky += dot(light, vec3(1.0 / 3.0));
    }

    vec3 stopped = 1.0 - let_through / float(short_rays);
    imageStore(u_density, texel, vec4(stopped, met / float(short_rays)));
    imageStore(u_color, texel, met > 0.0 ? vec4(colour / met, leaves / met) : fill);
    vec3 outwards = dot(normal, normal) > 1e-6 ? normalize(normal) : normalize(centre - heart.xyz + vec3(0.0, 1e-3, 0.0));
    imageStore(u_normal, texel, vec4(octEncode(outwards), sky / float(sky_rays), 1.0));

}
