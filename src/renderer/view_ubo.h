#ifndef UBO_DESCRIPTOR_H
#define UBO_DESCRIPTOR_H

#include "glm/glm.hpp"

namespace dy {

struct ViewUBO {

    glm::mat4 projection;
    glm::mat4 view;
    glm::vec3 direction;
    
};

}

#endif
