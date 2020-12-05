#ifndef UBO_DESCRIPTOR_H
#define UBO_DESCRIPTOR_H

#include "glm/glm.hpp"

struct ViewUBO {

    glm::mat4 view_projection;
    glm::vec4 position;
    
};

#endif
