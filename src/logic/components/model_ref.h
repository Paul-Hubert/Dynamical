#ifndef DY_MODEL_REF_H
#define DY_MODEL_REF_H

#include <glm/glm.hpp>

#include <string>

namespace dy {

// A glTF model to draw at the entity's Transform, by path under resources/.
// dynamical resolves it into a ModelRenderable; headless runs ignore it.
struct ModelRef {
    std::string path;
    glm::vec4 color = glm::vec4(1.f);
};

}

#endif
