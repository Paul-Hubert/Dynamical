#version 450

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 viewproj;
    vec4 position;
};

void main() {

    gl_Position = viewproj * vec4(a_pos, 1.0);
    v_position = a_pos;
    v_normal = a_normal;
    
}
