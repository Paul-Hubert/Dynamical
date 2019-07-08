#version 450

#extension GL_KHR_shader_subgroup_arithmetic: enable
#extension GL_KHR_shader_subgroup_ballot: enable

layout (local_size_x = 8, local_size_y = 4, local_size_z = 8) in;

const vec3 offsets[24] = {
    // 0
    vec3(0,0,0),
    vec3(1,0,0),
    // 1
    vec3(1,0,0),
    vec3(1,0,1),
    // 2
    vec3(1,0,1),
    vec3(0,0,1),
    // 3
    vec3(0,0,1),
    vec3(0,0,0),
    // 4
    vec3(0,1,0),
    vec3(1,1,0),
    // 5
    vec3(1,1,0),
    vec3(1,1,1),
    // 6
    vec3(1,1,1),
    vec3(0,1,1),
    // 7
    vec3(0,1,1),
    vec3(0,1,0),
    // 8
    vec3(0,0,0),
    vec3(0,1,0),
    // 9
    vec3(1,0,0),
    vec3(1,1,0),
    // 10
    vec3(1,0,1),
    vec3(1,1,1),
    // 11
    vec3(0,0,1),
    vec3(0,1,1)
};

const uint trinum[256] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 2, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 3, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 3, 2, 3, 3, 2, 3, 4, 4, 3, 3, 4, 4, 3, 4, 5, 5, 2, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 3, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 4, 2, 3, 3, 4, 3, 4, 2, 3, 3, 4, 4, 5, 4, 5, 3, 2, 3, 4, 4, 3, 4, 5, 3, 2, 4, 5, 5, 4, 5, 2, 4, 1, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 3, 2, 3, 3, 4, 3, 4, 4, 5, 3, 2, 4, 3, 4, 3, 5, 2, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 4, 3, 4, 4, 3, 4, 5, 5, 4, 4, 3, 5, 2, 5, 4, 2, 1, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 2, 3, 3, 2, 3, 4, 4, 5, 4, 5, 5, 2, 4, 3, 5, 4, 3, 2, 4, 1, 3, 4, 4, 5, 4, 5, 3, 4, 4, 5, 5, 2, 3, 4, 2, 1, 2, 3, 3, 2, 3, 4, 2, 1, 3, 2, 4, 1, 2, 1, 1, 0};

const ivec3 CHUNK_SIZES = ivec3(8*10, 4*8, 8*10);

layout(set = 0, binding = 0) uniform isamplerBuffer triTable;

layout(std430, set=1, binding = 0) writeonly buffer Tris {
    vec4 tril[];
};

layout(std430, set=1, binding = 1) coherent buffer IndirectDraw {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};


layout( push_constant ) uniform Args {
    vec3 pos;
    float time;
    float size;
};

float sq(float x) {
    return x*x;
}

const float k = 100.;

float sdf1(vec3 p) {
    return sq(18) - (sq(p.x-CHUNK_SIZES.x/2.)+sq(p.y-CHUNK_SIZES.y/2.)+sq(p.z-CHUNK_SIZES.z/2.)) - 30*sin(p.x+p.y+p.z+time/5.) - 20*cos(p.y+2*p.x-p.z+time/5.);
}

float sdf2(vec3 p) {
    return 10. - p.y - 3*sin((p.x+p.z)/20.) - 5*cos((float(p.x)-p.z)/20.);
}

float sdf(vec3 p) {
    return sdf2(p);
}

float values(vec3 p) {
    return sdf((p+pos)*size);
}

float values(float x, float y, float z) {
    return values(vec3(x, y, z));
}

void main() {
    
    if(gl_GlobalInvocationID.x == 0 && gl_GlobalInvocationID.y == 0 && gl_GlobalInvocationID.z == 0) {
        vertexCount = 0;
        instanceCount = 1;
        firstVertex = 0;
        firstInstance = 0;
    }
    barrier();
    
    uint val =    (values(gl_GlobalInvocationID.x  , gl_GlobalInvocationID.y  , gl_GlobalInvocationID.z  )>0 ? 1:0)
                | (values(gl_GlobalInvocationID.x+1, gl_GlobalInvocationID.y  , gl_GlobalInvocationID.z  )>0 ? 2:0)
                | (values(gl_GlobalInvocationID.x+1, gl_GlobalInvocationID.y  , gl_GlobalInvocationID.z+1)>0 ? 4:0)
                | (values(gl_GlobalInvocationID.x  , gl_GlobalInvocationID.y  , gl_GlobalInvocationID.z+1)>0 ? 8:0)
                | (values(gl_GlobalInvocationID.x  , gl_GlobalInvocationID.y+1, gl_GlobalInvocationID.z  )>0 ? 16:0)
                | (values(gl_GlobalInvocationID.x+1, gl_GlobalInvocationID.y+1, gl_GlobalInvocationID.z  )>0 ? 32:0)
                | (values(gl_GlobalInvocationID.x+1, gl_GlobalInvocationID.y+1, gl_GlobalInvocationID.z+1)>0 ? 64:0)
                | (values(gl_GlobalInvocationID.x  , gl_GlobalInvocationID.y+1, gl_GlobalInvocationID.z+1)>0 ? 128:0);
    
    uint num = trinum[val];
    if(num > 0 && !(gl_GlobalInvocationID.x >= CHUNK_SIZES.x-1 || gl_GlobalInvocationID.y >= CHUNK_SIZES.y-1 || gl_GlobalInvocationID.z >= CHUNK_SIZES.z-1)) {
    
        uint local_tri_index = subgroupExclusiveAdd(num*3);
        
        // Find out which active invocation has the highest ID
        uint highestActiveID = subgroupBallotFindMSB(subgroupBallot(true));

        uint global_tri_index = 0;

        // If we're the highest active ID
        if (highestActiveID == gl_SubgroupInvocationID) {
            // We need to carve out a slice of out_triangles for our triangle
            global_tri_index = atomicAdd(vertexCount, local_tri_index + num*3);
        }

        global_tri_index = subgroupMax(global_tri_index);
        
        for(int i = 0; i<num*3; i++) {
            int edge = texelFetch(triTable, int(val) * 16 + i).r * 2;
            vec3 a1 = gl_GlobalInvocationID.xyz + offsets[edge], a2 = gl_GlobalInvocationID.xyz + offsets[edge+1];
            //float density1 = values[int(a1.x * CHUNK_SIZE*CHUNK_SIZE + a1.y * CHUNK_SIZE + a1.z)];
            float density1 = values(a1);
            vec3 vertex = edge >= 0 ? mix(a1, a2, (-density1)/(values(a2) - density1)) : vec3(0);
            tril[global_tri_index + local_tri_index+i] = vec4((vertex+pos)*size, 1.0);
        }
        
    }
    
}
