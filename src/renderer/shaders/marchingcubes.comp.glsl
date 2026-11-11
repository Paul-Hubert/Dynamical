#version 450

// Marching cubes over one chunk : one invocation per cube.
// Triangles are appended to a vertex buffer whose size is the vertexCount of an indirect draw command,
// so the mesh never comes back to the CPU.

#extension GL_KHR_shader_subgroup_arithmetic: enable
#extension GL_KHR_shader_subgroup_ballot: enable

layout(local_size_x_id = 0, local_size_y_id = 1, local_size_z_id = 2) in;

// Density samples per chunk side, cubes + 1 + 2 * border
layout(constant_id = 3) const uint values_per_side = 67;

// Corner pairs of the 12 cube edges
const ivec3 offsets[24] = {
    ivec3(0,0,0), ivec3(1,0,0),
    ivec3(1,0,0), ivec3(1,0,1),
    ivec3(1,0,1), ivec3(0,0,1),
    ivec3(0,0,1), ivec3(0,0,0),
    ivec3(0,1,0), ivec3(1,1,0),
    ivec3(1,1,0), ivec3(1,1,1),
    ivec3(1,1,1), ivec3(0,1,1),
    ivec3(0,1,1), ivec3(0,1,0),
    ivec3(0,0,0), ivec3(0,1,0),
    ivec3(1,0,0), ivec3(1,1,0),
    ivec3(1,0,1), ivec3(1,1,1),
    ivec3(0,0,1), ivec3(0,1,1)
};

// Corner i of a cube, bit i of its configuration
const ivec3 corners[8] = {
    ivec3(0,0,0), ivec3(1,0,0), ivec3(1,0,1), ivec3(0,0,1),
    ivec3(0,1,0), ivec3(1,1,0), ivec3(1,1,1), ivec3(0,1,1)
};

// The edges as pairs of those corners : offsets[i] is corners[edge_corners[i]]
const int edge_corners[24] = {
    0,1, 1,2, 2,3, 3,0,
    4,5, 5,6, 6,7, 7,4,
    0,4, 1,5, 2,6, 3,7
};

// Triangles per cube configuration
const uint trinum[256] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 2, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 3, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 3, 2, 3, 3, 2, 3, 4, 4, 3, 3, 4, 4, 3, 4, 5, 5, 2, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 3, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 4, 2, 3, 3, 4, 3, 4, 2, 3, 3, 4, 4, 5, 4, 5, 3, 2, 3, 4, 4, 3, 4, 5, 3, 2, 4, 5, 5, 4, 5, 2, 4, 1, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 3, 2, 3, 3, 4, 3, 4, 4, 5, 3, 2, 4, 3, 4, 3, 5, 2, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 4, 3, 4, 4, 3, 4, 5, 5, 4, 4, 3, 5, 2, 5, 4, 2, 1, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 2, 3, 3, 2, 3, 4, 4, 5, 4, 5, 5, 2, 4, 3, 5, 4, 3, 2, 4, 1, 3, 4, 4, 5, 4, 5, 3, 4, 4, 5, 5, 2, 3, 4, 2, 1, 2, 3, 3, 2, 3, 4, 2, 1, 3, 2, 4, 1, 2, 1, 1, 0};

// Edge list per cube configuration, 16 entries each (resources/mclookup.bin)
layout(set = 0, binding = 0) uniform isamplerBuffer triTable;

layout(std430, set = 1, binding = 0) writeonly buffer Vertices {
    vec4 vertices[]; // two vec4 per vertex : (position, normal.x), (normal.y, curve), the normal octahedral (TerrainVertex)
};

layout(std430, set = 1, binding = 1) buffer IndirectDraw {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};

layout(std430, set = 1, binding = 2) readonly buffer Values {
    float val[];
};

layout(push_constant) uniform Args {
    ivec3 pos;          // world position of the chunk origin
    int size;           // world size of a cube
    uint max_vertices;  // capacity of the vertex buffer
    uint mode;          // 0 : march, 1 : clamp vertexCount to the capacity
};

const int border = 1;

float values(int x, int y, int z) {
    return val[(x + border) * values_per_side * values_per_side + (z + border) * values_per_side + (y + border)];
}

float values(ivec3 p) {
    return values(p.x, p.y, p.z);
}

// Octahedral : a unit vector in two values in -1..1 (decoded by terrain.vert.glsl and grass.vert.glsl)
vec2 encodeNormal(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 e = n.xz;
    if(n.y < 0.0) {
        e = (1.0 - abs(e.yx)) * vec2(e.x >= 0.0 ? 1.0 : -1.0, e.y >= 0.0 ? 1.0 : -1.0);
    }
    return e;
}

// Normal of the vertex on a grid edge (low : its sample of lower coordinates, axis : unit vector along it) :
// the sum of the normals of the triangles that meet there, each as long as twice its area, pointing out of
// the solid. Those triangles are in the four cubes around the edge, which the border of the densities covers
// on the sides of the chunk : the neighbour chunk finds the same normal there.
// The cubes come in the same order whichever of them asks : every copy of the vertex gets the same normal.
// MarchingCubes::vertexNormal of logic/terrain/marching_cubes.cpp
vec3 vertexNormal(ivec3 low, ivec3 axis) {

    ivec3 u = axis.zxy, v = axis.yzx;
    vec3 sum = vec3(0.0);

    for(int k = 0; k < 4; k++) {

        ivec3 cube = low - u * (k & 1) - v * (k >> 1);

        float d[8];
        uint config = 0u;
        for(int i = 0; i < 8; i++) {
            d[i] = values(cube + corners[i]);
            config |= d[i] > 0.0 ? 1u << i : 0u;
        }

        // The edge, among the twelve of this cube
        int own = -1;
        for(int e = 0; e < 12; e++) {
            ivec3 a = offsets[e * 2], b = offsets[e * 2 + 1];
            if(min(a, b) == low - cube && abs(b - a) == axis) {
                own = e;
            }
        }

        uint count = trinum[config] * 3u;
        for(uint i = 0u; i < count; i += 3u) {
            ivec3 edges = ivec3(
                texelFetch(triTable, int(config * 16u + i)).r,
                texelFetch(triTable, int(config * 16u + i + 1u)).r,
                texelFetch(triTable, int(config * 16u + i + 2u)).r);
            if(edges.x != own && edges.y != own && edges.z != own) {
                continue;
            }

            // Where the ground crosses the three edges, in cubes from low
            vec3 corner[3];
            for(int j = 0; j < 3; j++) {
                int a = edge_corners[edges[j] * 2], b = edge_corners[edges[j] * 2 + 1];
                float t = (-d[a]) / (d[b] - d[a]);
                corner[j] = mix(vec3(cube + corners[a] - low), vec3(cube + corners[b] - low), t);
            }
            sum += cross(corner[1] - corner[0], corner[2] - corner[0]);
        }

    }

    // The triangles of the table are counter clockwise seen from outside the solid
    return sum;

}

// How the ground bends at a sample : second derivatives of its height (xx, xz, zz), per metre.
// groundCurve of logic/terrain/ground_curve.h
vec3 groundCurve(ivec3 p) {
    float center = values(p);
    float h = 1.0 / float(size), hh = h * h;
    vec3 first = vec3(
        values(p.x + 1, p.y, p.z) - values(p.x - 1, p.y, p.z),
        values(p.x, p.y + 1, p.z) - values(p.x, p.y - 1, p.z),
        values(p.x, p.y, p.z + 1) - values(p.x, p.y, p.z - 1)) * (0.5 * h);
    if(first.y * first.y < 0.09 * dot(first, first) || first.y == 0.0) {
        return vec3(0.0);
    }

    float xx = (values(p.x + 1, p.y, p.z) - 2.0 * center + values(p.x - 1, p.y, p.z)) * hh;
    float yy = (values(p.x, p.y + 1, p.z) - 2.0 * center + values(p.x, p.y - 1, p.z)) * hh;
    float zz = (values(p.x, p.y, p.z + 1) - 2.0 * center + values(p.x, p.y, p.z - 1)) * hh;
    float xy = (values(p.x + 1, p.y + 1, p.z) - values(p.x + 1, p.y - 1, p.z) - values(p.x - 1, p.y + 1, p.z) + values(p.x - 1, p.y - 1, p.z)) * (0.25 * hh);
    float yz = (values(p.x, p.y + 1, p.z + 1) - values(p.x, p.y + 1, p.z - 1) - values(p.x, p.y - 1, p.z + 1) + values(p.x, p.y - 1, p.z - 1)) * (0.25 * hh);
    float xz = (values(p.x + 1, p.y, p.z + 1) - values(p.x + 1, p.y, p.z - 1) - values(p.x - 1, p.y, p.z + 1) + values(p.x - 1, p.y, p.z - 1)) * (0.25 * hh);

    float sx = -first.x / first.y, sz = -first.z / first.y;
    vec3 curve = vec3(
        -(xx + 2.0 * xy * sx + yy * sx * sx) / first.y,
        -(xz + xy * sz + yz * sx + yy * sx * sz) / first.y,
        -(zz + 2.0 * yz * sz + yy * sz * sz) / first.y);
    return clamp(curve, vec3(-1.0), vec3(1.0));
}

void main() {

    if(mode == 1) {
        if(gl_GlobalInvocationID == uvec3(0)) {
            vertexCount = min(vertexCount, max_vertices);
        }
        return;
    }

    ivec3 id = ivec3(gl_GlobalInvocationID);

    uint config =  (values(id + ivec3(0, 0, 0)) > 0 ? 1u   : 0u)
                 | (values(id + ivec3(1, 0, 0)) > 0 ? 2u   : 0u)
                 | (values(id + ivec3(1, 0, 1)) > 0 ? 4u   : 0u)
                 | (values(id + ivec3(0, 0, 1)) > 0 ? 8u   : 0u)
                 | (values(id + ivec3(0, 1, 0)) > 0 ? 16u  : 0u)
                 | (values(id + ivec3(1, 1, 0)) > 0 ? 32u  : 0u)
                 | (values(id + ivec3(1, 1, 1)) > 0 ? 64u  : 0u)
                 | (values(id + ivec3(0, 1, 1)) > 0 ? 128u : 0u);

    uint num = trinum[config];
    if(num == 0) {
        return;
    }

    // One atomic per subgroup : exclusive prefix sum for the local offset,
    // the last active invocation reserves the whole range and broadcasts its start
    uint local_index = subgroupExclusiveAdd(num * 3);
    uint highest = subgroupBallotFindMSB(subgroupBallot(true));

    uint global_index = 0;
    if(highest == gl_SubgroupInvocationID) {
        global_index = atomicAdd(vertexCount, local_index + num * 3);
    }
    global_index = subgroupMax(global_index);

    for(uint i = 0; i < num * 3; i++) {

        uint vertex_index = global_index + local_index + i;
        // Out of capacity : the clamp pass trims vertexCount, keep the writes in bounds
        if(vertex_index >= max_vertices) {
            break;
        }

        int edge = texelFetch(triTable, int(config) * 16 + int(i)).r * 2;
        ivec3 a1 = id + offsets[edge], a2 = id + offsets[edge + 1];
        float density1 = values(a1);
        float density2 = values(a2);
        float t = (-density1) / (density2 - density1);
        vec3 vertex = mix(vec3(a1), vec3(a2), t) * size + pos;

        // The sum of the triangles around the vertex : what the mesh looks like, and the same on every copy
        // of the vertex, so that it can be interpolated over the triangles
        vec3 norm = vertexNormal(min(a1, a2), abs(a2 - a1));
        if(dot(norm, norm) < 1e-12) {
            // Triangles without area (a density of exactly 0 on a sample) : minus the gradient of the density,
            // which grows toward the inside
            vec3 norm1 = vec3(
                values(a1.x - 1, a1.y, a1.z) - values(a1.x + 1, a1.y, a1.z),
                values(a1.x, a1.y - 1, a1.z) - values(a1.x, a1.y + 1, a1.z),
                values(a1.x, a1.y, a1.z - 1) - values(a1.x, a1.y, a1.z + 1)
            );
            vec3 norm2 = vec3(
                values(a2.x - 1, a2.y, a2.z) - values(a2.x + 1, a2.y, a2.z),
                values(a2.x, a2.y - 1, a2.z) - values(a2.x, a2.y + 1, a2.z),
                values(a2.x, a2.y, a2.z - 1) - values(a2.x, a2.y, a2.z + 1)
            );
            norm = mix(norm1, norm2, t);
        }
        vec2 normal = encodeNormal(normalize(norm));

        // How the ground bends there : where it falls away, the grass has the air behind it (grass.frag.glsl)
        vec3 curve = mix(groundCurve(a1), groundCurve(a2), t);

        vertices[vertex_index * 2] = vec4(vertex, normal.x);
        vertices[vertex_index * 2 + 1] = vec4(normal.y, curve);
    }

}
