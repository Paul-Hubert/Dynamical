#version 450

layout (location = 0) in vec2 v_uv;
layout (location = 1) in vec4 v_color;
layout (location = 2) in flat vec4 v_sphere;
layout (location = 3) in vec3 v_pos;
layout (location = 4) in vec3 v_cam;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform Camera {
    mat4 projection;
    mat4 view;
    vec3 position;  
} camera;

/*
float intersect(vec3 origin, vec3 direction)
{
    float3 oc = origin - v_sphere.xyz;
    float b = dot( oc, direction );
    float c = dot( oc, oc ) - v_sphere.w*v_sphere.w;
    float h = b*b - c;
    if( h<0.0 ) return -1.0;
    h = sqrt( h );
    return -b - h;
}
*/

void main() {

    vec3 v = v_pos-v_sphere.xyz;
    float len = dot(v,v); // length^2
    if(len < v_sphere.w*v_sphere.w) {
        outColor = v_color;

        vec3 dir = normalize(v_pos - v_cam);
        vec3 oc = v_cam - v_sphere.xyz;
        float b = dot(oc, dir);
        float c = dot(oc, oc) - v_sphere.w*v_sphere.w;
        float h = b*b - c;
        h = sqrt( h );
        float rlen = -b - h;
        vec3 rpos = v_cam + dir * rlen;

        vec3 normal = -normalize(rpos - v_sphere.xyz);

        const vec3 sun_dir = normalize(vec3(1, -1, 1));
        outColor.rgb *= max(dot(normal, sun_dir), 0.0) * 0.9 + 0.1;
    } else {
        discard;
    }
    
    
    
}

