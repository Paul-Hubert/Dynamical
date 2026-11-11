#ifndef UBO_DESCRIPTOR_H
#define UBO_DESCRIPTOR_H

#include "glm/glm.hpp"

namespace dy {

// Set 0, binding 0 of every scene pipeline. One per frame on desktop, one per frame and eye in VR.
// `View` of include/scene.glsl
struct ViewUBO {

    glm::mat4 view_projection;
    glm::vec4 position; // xyz : camera position in world space, w : time in seconds
    glm::vec4 tree_shade; // x : strength of the shade of the plants (TreeShade, binding 1), y : 1 / width of its map in metres,
                          // z : distance from the camera where it ends
    glm::mat4 inverse_view_projection;
    glm::mat4 previous_view_projection; // of the same eye, a frame before
    glm::vec4 previous_position; // xyz : where the eye was then
    glm::vec4 lighting; // the tier of the target (RenderSettings::Lighting) : x : traced range in metres, y : sun rays a pixel,
                        // z : shadowed torches a pixel

};

}

#endif
