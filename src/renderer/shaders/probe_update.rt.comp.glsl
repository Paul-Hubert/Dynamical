#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

// The probes of a brick (renderer/lighting/probe_volume.h, docs/lighting.md). One invocation a probe, in
// one of four ways :
//  - first : every probe of a new brick gets today's look, the open sky dimmed by the shade of the plants
//  - trace, then gather : the probes of a list send rays all around, an invocation a ray, then each
//    probe takes its rays together. A ray that misses reads the sky; one that hits
//    brings back the colour of what it hit times the light arriving there : the sun through a shadow ray,
//    the torches, and the light of the probes around the hit as they are now, which is what makes light
//    bounce more than once. The rays are blended into what the probe held
//  - fill : the probes of a list, in the ground next to the air, take the mean of their neighbours in the
//    air, so that filtering between probes does not pull black into the ground's light
//  - open : the probes of the open air take the sky as it is now

layout(local_size_x = 64) in;

#include "include/view.glsl"
#include "include/tree_shade.glsl"
#include "include/rays.glsl"
#include "include/probes.glsl"

// The page of the brick
layout(set = 1, binding = 0, rgba16f) uniform image3D u_light_r;
layout(set = 1, binding = 1, rgba16f) uniform image3D u_light_g;
layout(set = 1, binding = 2, rgba16f) uniform image3D u_light_b;
layout(set = 1, binding = 3, rgba8) uniform image3D u_direct;
layout(set = 1, binding = 4, r8ui) uniform readonly uimage3D u_classes;

// Indices of probes in their brick
layout(set = 2, binding = 0) readonly buffer Work {
    uint work[];
};

// What the rays of the probes of a dispatch found, a ray an invocation : rays x count of rgb radiance and
// a : 1 the sky, then sun_rays x count of how much of the sun the first rays of each probe saw
layout(set = 2, binding = 1) buffer Found {
    vec4 found[];
};

const uint mode_first = 0u;
const uint mode_trace = 1u;
const uint mode_fill = 2u;
const uint mode_open = 3u;
const uint mode_gather = 4u;

const uint sun_rays = 4u;

const uint class_rock = 0u;
const uint class_fill = 1u;
const uint class_active = 2u;
const uint class_open = 3u;

layout(push_constant) uniform Args {
    vec4 origin;        // xyz : of the brick in the world
    ivec4 atlas;        // xyz : texel of the first probe of the brick in its page
    uint mode;
    uint first;         // of the list
    uint count;         // probes of this dispatch
    uint rays;          // a probe
    float keep;         // part of what the probe held that stays
    uint seed;          // of the frame
};

const int brick_size = 65;

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

vec3 fibonacci(uint i, uint n) {
    float y = 1.0 - (2.0 * float(i) + 1.0) / float(n);
    float radius = sqrt(max(1.0 - y * y, 0.0));
    float angle = 2.39996323 * float(i);
    return vec3(radius * cos(angle), y, radius * sin(angle));
}

// A rotation drawn from three numbers in 0..1
mat3 randomRotation(float a, float b, float c) {
    float theta = 6.2831853 * a, phi = 6.2831853 * b;
    vec3 v = vec3(cos(phi) * sqrt(c), sin(phi) * sqrt(c), sqrt(1.0 - c));
    mat3 turn = mat3(cos(theta), sin(theta), 0.0, -sin(theta), cos(theta), 0.0, 0.0, 0.0, 1.0);
    // Householder : a mirror, times the turn around z, times -1
    return (2.0 * outerProduct(v, v) - mat3(1.0)) * turn;
}

// Colour of the ground for a bounce : grass where it is level, rock where it is steep
vec3 terrainColor(vec3 normal) {
    return mix(vec3(0.21, 0.2, 0.18), vec3(0.1, 0.15, 0.05), smoothstep(0.6, 0.85, normal.y));
}

// Light of the torches arriving on a surface, with their shadows
vec3 torchLight(vec3 position, vec3 normal) {
    vec3 sum = vec3(0.0);
    int lights = int(ambient_ground.w);
    for(int i = 0; i < lights; i++) {
        vec3 to = point_lights[i].position.xyz - position;
        float radius = point_lights[i].position.w;
        float d2 = dot(to, to);
        if(d2 >= radius * radius) continue;
        float d = sqrt(d2);
        float facing = dot(normal, to) / max(d, 1e-4);
        if(facing <= 0.0) continue;
        float window = 1.0 - d2 * d2 / (radius * radius * radius * radius);
        vec3 towards = to / max(d, 1e-4);
        sum += point_lights[i].color.rgb * (facing * window * window / (d2 + 0.25))
            * traceShadow(position + normal * 0.05, towards, max(d - 0.15, 0.0), ray_terrain | ray_plants | ray_static);
    }
    return sum;
}

// What a ray from a probe brings back. missed : it saw the sky
vec3 traceRadiance(vec3 origin, vec3 direction, inout uint state, out float missed) {

    // What moves is left out : the probes are slow to forget
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, 0u, ray_terrain | ray_plants | ray_static, origin, 0.02, direction, 1500.0);
    while(rayQueryProceedEXT(query)) {
        if(rayQueryGetIntersectionTypeEXT(query, false) != gl_RayQueryCandidateIntersectionTriangleEXT) continue;
        int index = rayQueryGetIntersectionInstanceCustomIndexEXT(query, false) + rayQueryGetIntersectionGeometryIndexEXT(query, false);
        RayGeometry geometry = ray_geometries[index];
        if(!rayLeaf(geometry, rayQueryGetIntersectionPrimitiveIndexEXT(query, false), rayQueryGetIntersectionBarycentricsEXT(query, false))) continue;
        // A leaf stops the ray as often as it is opaque
        float through = max(geometry.transmission.r, max(geometry.transmission.g, geometry.transmission.b));
        if(random(state) >= through) rayQueryConfirmIntersectionEXT(query);
    }

    if(rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
        missed = 1.0;
        return skyRadiance(direction);
    }
    missed = 0.0;

    int index = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true) + rayQueryGetIntersectionGeometryIndexEXT(query, true);
    RayGeometry geometry = ray_geometries[index];
    vec3 hit = origin + direction * rayQueryGetIntersectionTEXT(query, true);

    // The face that was hit, towards where the ray came from
    uvec3 corners = rayCorners(geometry, rayQueryGetIntersectionPrimitiveIndexEXT(query, true));
    mat4x3 to_world = rayQueryGetIntersectionObjectToWorldEXT(query, true);
    vec3 a = to_world * vec4(rayPosition(geometry, corners.x), 1.0);
    vec3 b = to_world * vec4(rayPosition(geometry, corners.y), 1.0);
    vec3 c = to_world * vec4(rayPosition(geometry, corners.z), 1.0);
    vec3 normal = cross(b - a, c - a);
    float area = length(normal);
    if(area <= 0.0) return vec3(0.0);
    normal /= area;
    if(dot(normal, direction) > 0.0) normal = -normal;

    vec3 color = geometry.kind == ray_kind_terrain ? terrainColor(normal) : geometry.color;

    vec3 light = vec3(0.0);
    float facing = dot(normal, sun_direction);
    if(facing > 0.0 && dot(light_color.rgb, vec3(1.0)) > 0.0) {
        light += light_color.rgb * facing * traceShadow(hit + normal * 0.05, sun_direction, 4000.0, ray_terrain | ray_plants | ray_static);
    }
    vec3 through = vec3(0.0);
    if(geometry.texture >= 0 && facing < 0.0 && dot(light_color.rgb, vec3(1.0)) > 0.0) {
        // A leaf seen from under : the sun on its other side comes through it, in its colour
        through = geometry.transmission * light_color.rgb * (-facing)
            * traceShadow(hit - normal * 0.05, sun_direction, 4000.0, ray_terrain | ray_plants | ray_static);
    }
    light += torchLight(hit, normal);
    // The light the probes around the hit hold now : a bounce more at every update
    light += probeLight(hit, normal, mix(ambient_ground.rgb, ambient_sky.rgb, normal.y * 0.5 + 0.5));

    return color * light + through;

}

void store(ivec3 texel, vec4 r, vec4 g, vec4 b, vec4 direct) {
    imageStore(u_light_r, texel, r);
    imageStore(u_light_g, texel, g);
    imageStore(u_light_b, texel, b);
    imageStore(u_direct, texel, direct);
}

void main() {

    // A probe an invocation, or when tracing a ray an invocation
    uint slot = mode == mode_trace ? gl_GlobalInvocationID.x / rays : gl_GlobalInvocationID.x;
    if(slot >= count) return;

    bool listed = mode == mode_trace || mode == mode_fill || mode == mode_gather;
    uint index = listed ? work[first + slot] : slot;
    ivec3 probe = ivec3(int(index) % brick_size, (int(index) / brick_size) % brick_size, int(index) / (brick_size * brick_size));
    ivec3 texel = atlas.xyz + probe;
    vec3 position = origin.xyz + vec3(probe) * probe_spacing;

    if(mode == mode_first) {
        // What the shaders showed before the probes : the sky and the ground, less of both and of the
        // sun under the plants
        float shade = treeShade(position, 0.0);
        store(texel, sky_light[0] * (1.0 - 0.35 * shade), sky_light[1] * (1.0 - 0.35 * shade), sky_light[2] * (1.0 - 0.35 * shade),
            vec4(vec3(1.0 - shade), 1.0 - 0.35 * shade));
        return;
    }

    if(mode == mode_open) {
        if(imageLoad(u_classes, texel).r == class_open) store(texel, sky_light[0], sky_light[1], sky_light[2], vec4(1.0));
        return;
    }

    if(mode == mode_fill) {
        vec4 r = vec4(0.0), g = vec4(0.0), b = vec4(0.0), direct = vec4(0.0);
        float weight = 0.0;
        for(int z = -1; z <= 1; z++) {
            for(int y = -1; y <= 1; y++) {
                for(int x = -1; x <= 1; x++) {
                    ivec3 near = probe + ivec3(x, y, z);
                    if(any(lessThan(near, ivec3(0))) || any(greaterThanEqual(near, ivec3(brick_size)))) continue;
                    if(imageLoad(u_classes, atlas.xyz + near).r < class_active) continue;
                    r += imageLoad(u_light_r, atlas.xyz + near);
                    g += imageLoad(u_light_g, atlas.xyz + near);
                    b += imageLoad(u_light_b, atlas.xyz + near);
                    direct += imageLoad(u_direct, atlas.xyz + near);
                    weight += 1.0;
                }
            }
        }
        if(weight > 0.0) store(texel, r / weight, g / weight, b / weight, direct / weight);
        return;
    }

    // Rays all around : an even set, turned differently for each probe and each time
    uint state = hash(seed * 9781u + index * 6271u + uint(atlas.x + 131 * atlas.y + 17161 * atlas.z) * 31u + 17u);
    mat3 turn = randomRotation(random(state), random(state), random(state));

    if(mode == mode_trace) {
        uint ray = gl_GlobalInvocationID.x % rays;
        state = hash(state + ray * 7919u);
        float missed;
        vec3 radiance = traceRadiance(position, turn * fibonacci(ray, rays), state, missed);
        found[slot * rays + ray] = vec4(radiance, missed);

        // The first rays of a probe also look at the disc of the sun : how much of it the probe sees,
        // through the leaves
        if(ray < sun_rays) {
            vec3 sun = vec3(1.0);
            if(dot(light_color.rgb, vec3(1.0)) > 0.0) {
                vec3 side = normalize(cross(sun_direction, abs(sun_direction.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
                vec3 over = cross(sun_direction, side);
                float radius = light_direction.w * sqrt(random(state));
                float angle = 6.2831853 * random(state);
                vec3 direction = normalize(sun_direction + (side * cos(angle) + over * sin(angle)) * radius);
                sun = traceShadow(position, direction, 4000.0, ray_terrain | ray_plants | ray_static);
            }
            found[count * rays + slot * sun_rays + ray] = vec4(sun, 0.0);
        }
        return;
    }

    // Gather : the rays of the probe, folded with the cosine of a surface (ProbeLight)
    vec4 r = vec4(0.0), g = vec4(0.0), b = vec4(0.0);
    float sky = 0.0;
    for(uint i = 0u; i < rays; i++) {
        vec4 ray = found[slot * rays + i];
        vec4 basis = vec4(1.0, 2.0 * (turn * fibonacci(i, rays)));
        r += basis * ray.r;
        g += basis * ray.g;
        b += basis * ray.b;
        sky += ray.a;
    }
    float each = 1.0 / float(rays);
    r *= each;
    g *= each;
    b *= each;

    vec3 sun = vec3(0.0);
    uint seen = min(sun_rays, rays);
    for(uint i = 0u; i < seen; i++) sun += found[count * rays + slot * sun_rays + i].rgb;
    vec4 direct = vec4(sun / float(seen), sky * each);

    if(keep > 0.0) {
        r = mix(r, imageLoad(u_light_r, texel), keep);
        g = mix(g, imageLoad(u_light_g, texel), keep);
        b = mix(b, imageLoad(u_light_b, texel), keep);
        direct = mix(direct, imageLoad(u_direct, texel), keep);
    }
    store(texel, r, g, b, direct);

}
