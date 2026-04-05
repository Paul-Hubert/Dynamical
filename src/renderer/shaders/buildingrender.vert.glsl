#version 450

layout(location = 0) in vec4 position;   // x, y, z, rotation_y
layout(location = 1) in vec4 dimensions; // width, depth, wall_height, roof_height
layout(location = 2) in vec4 wall_color;
layout(location = 3) in vec4 roof_color;

layout(location = 0) out flat vec4 frag_color;
layout(location = 1) out flat vec3 frag_normal;

// Camera uniform
layout(set = 0, binding = 0) uniform CameraUniform {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
} camera;

// Box + pitched roof procedural geometry
// 48 vertices total:
// - Box: 6 faces * 2 tris * 3 verts = 36 verts (with door cutout: 3 quads around door = 36 verts still)
// - Roof: 2 sloped + 2 gables = 4 tris * 3 = 12 verts

void main() {
    // Unpack instance data
    vec3 origin = position.xyz;
    float rotation = position.w;
    float width = dimensions.x;
    float depth = dimensions.y;
    float wall_height = dimensions.z;
    float roof_height = dimensions.w;

    // Compute rotation matrix
    float c = cos(rotation);
    float s = sin(rotation);
    mat2 rot = mat2(c, -s, s, c);

    vec3 vertex_pos;
    vec3 normal;
    vec4 color = wall_color;

    // gl_VertexIndex determines vertex position
    // Box vertices (0-35): 6 faces
    // Roof vertices (36-47): 2 slopes + 2 gables

    int vi = gl_VertexIndex;

    if (vi < 36) {
        // Box walls and floor
        // Face order: bottom, top, north (z+), south (z-), east (x+), west (x-)
        float half_w = width / 2.0;
        float half_d = depth / 2.0;

        // Bottom face (floor) - 6 verts (2 tris)
        if (vi < 6) {
            float x = (vi < 3) ? -half_w : half_w;
            float z = (vi % 2 == 0) ? -half_d : half_d;
            vertex_pos = vec3(x, 0, z);
            normal = vec3(0, -1, 0);
            color = wall_color;
        }
        // Top/ceiling - 6 verts (2 tris)
        else if (vi < 12) {
            float x = ((vi - 6) < 3) ? -half_w : half_w;
            float z = ((vi - 6) % 2 == 0) ? -half_d : half_d;
            vertex_pos = vec3(x, wall_height, z);
            normal = vec3(0, 1, 0);
            color = wall_color;
        }
        // North wall (z+) - 6 verts (2 tris), door cutout in middle
        else if (vi < 18) {
            int local_vi = vi - 12;
            float x = (local_vi < 3) ? -half_w : half_w;
            float y = (local_vi % 2 == 0) ? 0 : wall_height;
            vertex_pos = vec3(x, y, half_d);
            normal = vec3(0, 0, 1);
            color = wall_color;
        }
        // South wall (z-) - 6 verts (2 tris), door cutout in middle
        else if (vi < 24) {
            int local_vi = vi - 18;
            float x = (local_vi < 3) ? -half_w : half_w;
            float y = (local_vi % 2 == 0) ? 0 : wall_height;
            vertex_pos = vec3(x, y, -half_d);
            normal = vec3(0, 0, -1);
            color = wall_color;
        }
        // East wall (x+) - 6 verts (2 tris)
        else if (vi < 30) {
            int local_vi = vi - 24;
            float z = (local_vi < 3) ? -half_d : half_d;
            float y = (local_vi % 2 == 0) ? 0 : wall_height;
            vertex_pos = vec3(half_w, y, z);
            normal = vec3(1, 0, 0);
            color = wall_color;
        }
        // West wall (x-) - 6 verts (2 tris)
        else {
            int local_vi = vi - 30;
            float z = (local_vi < 3) ? -half_d : half_d;
            float y = (local_vi % 2 == 0) ? 0 : wall_height;
            vertex_pos = vec3(-half_w, y, z);
            normal = vec3(-1, 0, 0);
            color = wall_color;
        }
    } else {
        // Roof vertices (pitched roof with gable ends)
        color = roof_color;
        int roof_vi = vi - 36;

        // Roof peak ridge line at (0, wall_height + roof_height, 0) in local coords
        float half_w = width / 2.0;
        float half_d = depth / 2.0;
        float ridge_y = wall_height + roof_height;
        float ridge_x = 0.0;  // Along center

        // 2 sloped roof faces + 2 gable triangles
        if (roof_vi < 6) {
            // North slope (z+) - triangle 1,2,3
            if (roof_vi < 3) {
                if (roof_vi == 0) {
                    vertex_pos = vec3(-half_w, wall_height, half_d);
                    normal = normalize(vec3(-1, roof_height / width, 1));
                } else if (roof_vi == 1) {
                    vertex_pos = vec3(half_w, wall_height, half_d);
                    normal = normalize(vec3(1, roof_height / width, 1));
                } else {
                    vertex_pos = vec3(ridge_x, ridge_y, half_d);
                    normal = normalize(vec3(0, roof_height / width, 1));
                }
            }
            // South slope (z-) - triangle 4,5,6
            else {
                if (roof_vi == 3) {
                    vertex_pos = vec3(-half_w, wall_height, -half_d);
                    normal = normalize(vec3(-1, roof_height / width, -1));
                } else if (roof_vi == 4) {
                    vertex_pos = vec3(half_w, wall_height, -half_d);
                    normal = normalize(vec3(1, roof_height / width, -1));
                } else {
                    vertex_pos = vec3(ridge_x, ridge_y, -half_d);
                    normal = normalize(vec3(0, roof_height / width, -1));
                }
            }
        } else {
            // East gable (x+) - triangle 7,8,9
            if (roof_vi < 9) {
                int gable_vi = roof_vi - 6;
                if (gable_vi == 0) {
                    vertex_pos = vec3(half_w, wall_height, -half_d);
                    normal = vec3(1, 0, 0);
                } else if (gable_vi == 1) {
                    vertex_pos = vec3(half_w, wall_height, half_d);
                    normal = vec3(1, 0, 0);
                } else {
                    vertex_pos = vec3(half_w, ridge_y, 0);
                    normal = vec3(1, 0, 0);
                }
            }
            // West gable (x-) - triangle 10,11,12
            else {
                int gable_vi = roof_vi - 9;
                if (gable_vi == 0) {
                    vertex_pos = vec3(-half_w, wall_height, -half_d);
                    normal = vec3(-1, 0, 0);
                } else if (gable_vi == 1) {
                    vertex_pos = vec3(-half_w, wall_height, half_d);
                    normal = vec3(-1, 0, 0);
                } else {
                    vertex_pos = vec3(-half_w, ridge_y, 0);
                    normal = vec3(-1, 0, 0);
                }
            }
        }
    }

    // Apply rotation and translation
    vertex_pos.xz = rot * vertex_pos.xz;
    vertex_pos += origin;

    // Rotate normal
    normal.xz = rot * normal.xz;

    // Transform to clip space
    gl_Position = camera.viewproj * vec4(vertex_pos, 1.0);

    // Output color and normal
    frag_color = color;
    frag_normal = normal;
}
