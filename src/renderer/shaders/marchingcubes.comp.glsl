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

// Triangles per cube configuration
const uint trinum[256] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 2, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 3, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 3, 2, 3, 3, 2, 3, 4, 4, 3, 3, 4, 4, 3, 4, 5, 5, 2, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 3, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 4, 2, 3, 3, 4, 3, 4, 2, 3, 3, 4, 4, 5, 4, 5, 3, 2, 3, 4, 4, 3, 4, 5, 3, 2, 4, 5, 5, 4, 5, 2, 4, 1, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 3, 2, 3, 3, 4, 3, 4, 4, 5, 3, 2, 4, 3, 4, 3, 5, 2, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 4, 3, 4, 4, 3, 4, 5, 5, 4, 4, 3, 5, 2, 5, 4, 2, 1, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 2, 3, 3, 2, 3, 4, 4, 5, 4, 5, 5, 2, 4, 3, 5, 4, 3, 2, 4, 1, 3, 4, 4, 5, 4, 5, 3, 4, 4, 5, 5, 2, 3, 4, 2, 1, 2, 3, 3, 2, 3, 4, 2, 1, 3, 2, 4, 1, 2, 1, 1, 0};

// Edge list per cube configuration, 16 entries each (resources/mclookup.bin)
layout(set = 0, binding = 0) uniform isamplerBuffer triTable;

layout(std430, set = 1, binding = 0) writeonly buffer Vertices {
    vec4 vertices[]; // two vec4 per vertex : (position, normal.x), (normal.yz, uv)
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

        // Density grows toward the inside : the outward normal is minus its gradient
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
        vec3 normal = normalize(mix(norm1, norm2, t));

        vec2 uv = vertex.xz / 32.;

        vertices[vertex_index * 2] = vec4(vertex, normal.x);
        vertices[vertex_index * 2 + 1] = vec4(normal.yz, uv);
    }

}
