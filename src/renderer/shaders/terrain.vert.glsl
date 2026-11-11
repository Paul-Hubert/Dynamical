#version 450

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_normal;  // octahedral (marchingcubes.comp.glsl)
layout(location = 2) in vec3 a_curve;   // how the ground bends, for the grass

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos; // w : time
};

vec3 decodeNormal(vec2 e) {
    vec3 n = vec3(e.x, 1.0 - abs(e.x) - abs(e.y), e.y);
    float fold = max(-n.y, 0.0);
    n.x += n.x >= 0.0 ? -fold : fold;
    n.z += n.z >= 0.0 ? -fold : fold;
    return normalize(n);
}

void main() {

    gl_Position = viewproj * vec4(a_pos, 1.0);
    v_position = a_pos;
    v_normal = decodeNormal(a_normal);

}
