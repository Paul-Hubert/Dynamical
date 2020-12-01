#version 450

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(std140, set = 0, binding = 0) uniform Camera {
    mat4 viewproj;
    vec4 position;
} camera;

struct PerModel {
    mat4 basis;
    vec4 position;
};
layout(std140, set = 1, binding = 0) uniform Transform {
    PerModel per_model[100];
} transform;

void main() {

    v_position = mat3(transform.per_model[gl_InstanceIndex].basis) * a_pos + vec3(transform.per_model[gl_InstanceIndex].position);
    v_position.y = -v_position.y;
    v_normal = a_normal;
    gl_Position = camera.viewproj * vec4(v_position, 1.0);
    
}
