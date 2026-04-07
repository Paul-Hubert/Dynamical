#version 450

layout(location = 0) in vec4 position;   // x, y, z (terrain height), rotation
layout(location = 1) in vec4 dimensions; // width, depth, wall_height, roof_height
layout(location = 2) in vec4 wall_color;
layout(location = 3) in vec4 roof_color;

layout(location = 0) out flat vec4 frag_color;
layout(location = 1) out flat vec3 frag_normal;

// Camera uniform — must match object shader layout (projection, then view)
layout(set = 0, binding = 0) uniform Camera {
    mat4 projection;
    mat4 view;
} camera;

// Box + pyramid roof procedural geometry
// 48 vertices total:
// - Box: 6 faces * 6 verts = 36 verts
// - Roof: 4 triangles * 3 verts = 12 verts

void main() {
    // Unpack instance data
    vec3 origin = position.xyz;
    float rotation = position.w;
    float width = dimensions.x;
    float depth = dimensions.y;
    float wall_height = dimensions.z;
    float roof_height = dimensions.w;

    // Rotation matrix around Z axis (yaw in XY plane)
    float c = cos(rotation);
    float s = sin(rotation);
    mat2 rot = mat2(c, -s, s, c);

    vec3 vertex_pos;
    vec3 normal;
    vec4 color = wall_color;

    int vi = gl_VertexIndex;

    // Quad vertex lookup: 2 triangles = 6 vertices per face
    // Tri 1: (0,0), (1,0), (0,1)  Tri 2: (1,0), (1,1), (0,1)
    const int qx[6] = int[6](0, 1, 0, 1, 1, 0);
    const int qy[6] = int[6](0, 0, 1, 0, 1, 1);

    float half_w = width / 2.0;
    float half_d = depth / 2.0;

    if (vi < 36) {
        // Box faces: X = width, Y = depth, Z = height
        int face = vi / 6;
        int lv = vi % 6;
        float a = float(qx[lv]);
        float b = float(qy[lv]);

        if (face == 0) {
            // Bottom face (z=0)
            vertex_pos = vec3(mix(-half_w, half_w, a), mix(-half_d, half_d, b), 0.0);
            normal = vec3(0, 0, -1);
        }
        else if (face == 1) {
            // Top face (z=wall_height)
            vertex_pos = vec3(mix(-half_w, half_w, a), mix(-half_d, half_d, b), wall_height);
            normal = vec3(0, 0, 1);
        }
        else if (face == 2) {
            // North wall (y=+half_d)
            vertex_pos = vec3(mix(-half_w, half_w, a), half_d, mix(0.0, wall_height, b));
            normal = vec3(0, 1, 0);
        }
        else if (face == 3) {
            // South wall (y=-half_d)
            vertex_pos = vec3(mix(-half_w, half_w, a), -half_d, mix(0.0, wall_height, b));
            normal = vec3(0, -1, 0);
        }
        else if (face == 4) {
            // East wall (x=+half_w)
            vertex_pos = vec3(half_w, mix(-half_d, half_d, a), mix(0.0, wall_height, b));
            normal = vec3(1, 0, 0);
        }
        else {
            // West wall (x=-half_w)
            vertex_pos = vec3(-half_w, mix(-half_d, half_d, a), mix(0.0, wall_height, b));
            normal = vec3(-1, 0, 0);
        }
    } else {
        // Roof: 4 pyramid triangles from wall top edges to center peak
        color = roof_color;
        int roof_vi = vi - 36;
        int face = roof_vi / 3;
        int lv = roof_vi % 3;
        float peak_z = wall_height + roof_height;

        if (face == 0) {
            // East face
            if (lv == 0) vertex_pos = vec3(half_w, -half_d, wall_height);
            else if (lv == 1) vertex_pos = vec3(half_w, half_d, wall_height);
            else vertex_pos = vec3(0.0, 0.0, peak_z);
            normal = normalize(vec3(roof_height, 0, half_w));
        }
        else if (face == 1) {
            // West face
            if (lv == 0) vertex_pos = vec3(-half_w, half_d, wall_height);
            else if (lv == 1) vertex_pos = vec3(-half_w, -half_d, wall_height);
            else vertex_pos = vec3(0.0, 0.0, peak_z);
            normal = normalize(vec3(-roof_height, 0, half_w));
        }
        else if (face == 2) {
            // North face
            if (lv == 0) vertex_pos = vec3(half_w, half_d, wall_height);
            else if (lv == 1) vertex_pos = vec3(-half_w, half_d, wall_height);
            else vertex_pos = vec3(0.0, 0.0, peak_z);
            normal = normalize(vec3(0, roof_height, half_d));
        }
        else {
            // South face
            if (lv == 0) vertex_pos = vec3(-half_w, -half_d, wall_height);
            else if (lv == 1) vertex_pos = vec3(half_w, -half_d, wall_height);
            else vertex_pos = vec3(0.0, 0.0, peak_z);
            normal = normalize(vec3(0, -roof_height, half_d));
        }
    }

    // Rotate in XY plane (around Z/height axis)
    vertex_pos.xy = rot * vertex_pos.xy;
    vertex_pos += origin;

    // Rotate normal to match
    normal.xy = rot * normal.xy;

    // Transform to clip space
    gl_Position = camera.projection * camera.view * vec4(vertex_pos, 1.0);

    // Output color and normal
    frag_color = color;
    frag_normal = normal;
}
