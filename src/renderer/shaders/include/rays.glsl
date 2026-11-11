// Rays against the scene (renderer/lighting/ray_scene.h). Only in the shaders compiled with RAY_TRACING,
// which declare these extensions before anything else :
//   GL_EXT_ray_query, GL_EXT_buffer_reference, GL_EXT_scalar_block_layout, GL_EXT_nonuniform_qualifier

layout(set = 0, binding = 4) uniform accelerationStructureEXT u_scene;

// Vertices of a plant (VegetationVertex) and indices, as the species keeps them
struct PlantVertex {
    vec3 position;
    uint normal;
    uint tangent;
    vec2 uv;
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer PlantVertices {
    PlantVertex v[];
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Indices {
    uint i[];
};

// What a shader knows of a hit : at instanceCustomIndex + geometry index (RayScene::GeometryEntry)
const uint ray_kind_terrain = 0u;
const uint ray_kind_solid = 1u;
const uint ray_kind_plant = 2u;

struct RayGeometry {
    PlantVertices vertices;     // where the indices count from
    Indices indices;
    int texture;                // of u_ray_textures : its alpha is tested against alpha_cutoff, -1 none
    float alpha_cutoff;
    vec3 color;                 // of the light it sends back
    uint kind;
    vec3 transmission;          // of the light that goes through it where its texture is
    uint pad;
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

// Texture coordinates of a point of a triangle of a plant
vec2 plantUV(RayGeometry geometry, int primitive, vec2 barycentrics) {
    uint a = geometry.indices.i[3 * primitive], b = geometry.indices.i[3 * primitive + 1], c = geometry.indices.i[3 * primitive + 2];
    return geometry.vertices.v[a].uv * (1.0 - barycentrics.x - barycentrics.y)
        + geometry.vertices.v[b].uv * barycentrics.x + geometry.vertices.v[c].uv * barycentrics.y;
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
        if(geometry.texture >= 0) {
            vec2 uv = plantUV(geometry, rayQueryGetIntersectionPrimitiveIndexEXT(query, false), rayQueryGetIntersectionBarycentricsEXT(query, false));
            // A level under the sharpest : shadows do not need each tooth of a leaf
            float alpha = textureLod(u_ray_textures[nonuniformEXT(geometry.texture)], uv, 1.0).a;
            if(alpha < geometry.alpha_cutoff) continue;
        }
        light *= geometry.transmission;
        if(max(light.r, max(light.g, light.b)) < 0.02) {
            rayQueryTerminateEXT(query);
            return vec3(0.0);
        }
    }

    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT ? light : vec3(0.0);

}
