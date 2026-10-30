#ifndef UBO_DESCRIPTOR_H
#define UBO_DESCRIPTOR_H

#include "glm/glm.hpp"

namespace dy {

// Set 0, binding 0 of every scene pipeline. One per frame on desktop, one per frame and eye in VR.
struct ViewUBO {

    glm::mat4 view_projection;
    glm::vec4 position; // xyz : camera position in world space, w : time in seconds

};

}

#endif
