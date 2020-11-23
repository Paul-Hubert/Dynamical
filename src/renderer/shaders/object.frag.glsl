#version 450

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;

layout(location = 0) out vec4 outColor;

//layout(set = 1, binding = 0) uniform sampler2DArray u_color;

void main() {
    
    vec3 color = vec3(1,0,0);

    vec3 normal = normalize(v_normal);

    float light = abs(dot(normal, vec3(0.5,-1.,0.5)));
    outColor = vec4(color * (light*0.7 + 0.3), 1);
    
}
