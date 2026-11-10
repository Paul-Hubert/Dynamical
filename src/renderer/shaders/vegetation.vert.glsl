#version 450

// SpeedTree geometry of the vegetation library (tools/speedtree_import.py), without wind

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_normal;      // w : sign of the bitangent
layout(location = 2) in vec4 a_tangent;     // w : ambient occlusion baked by SpeedTree
layout(location = 3) in vec2 a_uv;

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec4 v_tangent;    // w : sign of the bitangent
layout(location = 3) out vec2 v_uv;
layout(location = 4) out vec2 v_shade;      // x : ambient occlusion, y : fade of a billboard card
layout(location = 5) out float v_above;     // height above the foot of the plant

layout(std140, set = 0, binding = 0) uniform View {
    mat4 viewproj;
    vec4 viewpos; // w : time
};

layout(push_constant) uniform Plant {
    mat4 model;
    vec4 tint;  // w : height of the plant
    vec4 params; // x : alpha cutoff, y : billboard cards, z : normal map, w : subsurface
};

void main() {

    // Plants are rotated and scaled uniformly
    mat3 rotation = mat3(model);
    vec3 normal = normalize(rotation * a_normal.xyz);
    vec3 tangent = normalize(rotation * a_tangent.xyz);
    vec4 world = model * vec4(a_position, 1.0);

    float fade = 1.0;

    if(params.y > 0.5) {

        // Billboard : SpeedTree bakes the plant on cards facing 8 directions and one seen from above.
        // Only the cards facing the camera are kept, their edges eaten by the alpha test as they turn away.
        vec3 to_camera = normalize(viewpos.xyz - model[3].xyz);
        bool top_down = a_normal.y > 0.5;

        float facing = max(dot(to_camera, normal), 0.0);
        facing *= facing;
        facing *= facing;
        fade = clamp(facing + (top_down ? 0.38 : 0.18), 0.0, 1.0);

        // Lit as if the card faced the camera, else the cards show as bands of different shades
        if(top_down) {
            normal = normalize(normal + to_camera);
        } else {
            vec3 bitangent = cross(normal, tangent) * a_normal.w;
            vec3 right = cross(to_camera, bitangent);
            vec3 turned = cross(bitangent, right);
            if(dot(turned, turned) > 1e-6) normal = normalize(turned);
        }
    }

    v_position = world.xyz;
    v_normal = normal;
    v_tangent = vec4(tangent, a_normal.w);
    v_uv = a_uv;
    v_shade = vec2(a_tangent.w, fade);
    v_above = world.y - model[3].y;

    // A card turned away is not drawn at all
    gl_Position = fade < params.x ? vec4(0.0, 0.0, 2.0, 1.0) : viewproj * world;

}
