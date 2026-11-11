// Rays against the scene (renderer/lighting/ray_scene.h). Only in the shaders compiled with RAY_TRACING,
// which declare these extensions before anything else :
//   GL_EXT_ray_query, GL_EXT_buffer_reference, GL_EXT_scalar_block_layout, GL_EXT_nonuniform_qualifier

layout(set = 0, binding = 4) uniform accelerationStructureEXT u_scene;

// Vertices as floats, whatever they hold after their position, and indices
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer RayFloats {
    float f[];
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer RayIndices {
    uint i[];
};

// What a shader knows of a hit : at instanceCustomIndex + geometry index (RayScene::GeometryEntry)
const uint ray_kind_terrain = 0u;   // a chunk : a list of triangles, no indices
const uint ray_kind_solid = 1u;
const uint ray_kind_plant = 2u;     // VegetationVertex : texture coordinates at floats 5 and 6

struct RayGeometry {
    RayFloats vertices;         // where the indices count from
    RayIndices indices;
    int texture;                // of u_ray_textures : its alpha is tested against alpha_cutoff, -1 none
    float alpha_cutoff;
    vec3 color;                 // of the light it sends back
    uint kind;
    vec3 transmission;          // of the light that goes through it where its texture is
    uint stride;                // floats in a vertex
};
layout(set = 0, binding = 5, scalar) readonly buffer RayGeometries {
    RayGeometry ray_geometries[];
};

layout(set = 0, binding = 6) uniform sampler2D u_ray_textures[];

// Masks of the instances (RayScene::Mask)
const uint ray_terrain = 1u;
const uint ray_plants = 2u;
const uint ray_static = 4u;
const uint ray_dynamic = 8u;
const uint ray_all = 0xFFu;

// The three vertices of a triangle of a geometry
uvec3 rayCorners(RayGeometry geometry, int primitive) {
    if(geometry.kind == ray_kind_terrain) return uvec3(3 * primitive, 3 * primitive + 1, 3 * primitive + 2);
    return uvec3(geometry.indices.i[3 * primitive], geometry.indices.i[3 * primitive + 1], geometry.indices.i[3 * primitive + 2]);
}

vec3 rayPosition(RayGeometry geometry, uint vertex) {
    uint at = vertex * geometry.stride;
    return vec3(geometry.vertices.f[at], geometry.vertices.f[at + 1], geometry.vertices.f[at + 2]);
}

// Texture coordinates of a point of a triangle of a plant
vec2 plantUV(RayGeometry geometry, int primitive, vec2 barycentrics) {
    uvec3 corners = rayCorners(geometry, primitive) * geometry.stride + 5;
    return vec2(geometry.vertices.f[corners.x], geometry.vertices.f[corners.x + 1]) * (1.0 - barycentrics.x - barycentrics.y)
        + vec2(geometry.vertices.f[corners.y], geometry.vertices.f[corners.y + 1]) * barycentrics.x
        + vec2(geometry.vertices.f[corners.z], geometry.vertices.f[corners.z + 1]) * barycentrics.y;
}

// Whether a candidate of a ray is there : the alpha test of a leaf. A level under the sharpest, a ray does
// not need each tooth of it
bool rayLeaf(RayGeometry geometry, int primitive, vec2 barycentrics) {
    if(geometry.texture < 0) return true;
    return textureLod(u_ray_textures[nonuniformEXT(geometry.texture)], plantUV(geometry, primitive, barycentrics), 1.0).a >= geometry.alpha_cutoff;
}

// How much of a light reaches `origin` from `distance` away along `direction`, per colour : 0 behind
// something opaque. Leaves are not opaque : where their texture is they let a part of the light through,
// tinted. A product, so the order the ray meets them in does not matter and none is committed
vec3 traceShadow(vec3 origin, vec3 direction, float distance, uint mask) {

    vec3 light = vec3(1.0);

    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_scene, gl_RayFlagsTerminateOnFirstHitEXT, mask, origin, 0.0, direction, distance);
    while(rayQueryProceedEXT(query)) {
        if(rayQueryGetIntersectionTypeEXT(query, false) != gl_RayQueryCandidateIntersectionTriangleEXT) continue;

        int index = rayQueryGetIntersectionInstanceCustomIndexEXT(query, false) + rayQueryGetIntersectionGeometryIndexEXT(query, false);
        RayGeometry geometry = ray_geometries[index];
        if(!rayLeaf(geometry, rayQueryGetIntersectionPrimitiveIndexEXT(query, false), rayQueryGetIntersectionBarycentricsEXT(query, false))) continue;

        light *= geometry.transmission;
        if(max(light.r, max(light.g, light.b)) < 0.02) {
            rayQueryTerminateEXT(query);
            return vec3(0.0);
        }
    }

    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT ? light : vec3(0.0);

}
