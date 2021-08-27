#version 450

layout (location = 0) in vec2 v_uv;
layout (location = 1) in vec4 v_color;

layout(location = 0) out vec4 outColor;

void main() {
    
    if(length(v_uv-0.5) < 0.5) {
        outColor = v_color;
    } else {
        discard;
    }
    
    
    
}

