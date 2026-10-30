#ifndef DY_TRANSFORM_H
#define DY_TRANSFORM_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace dy {

class Transform {
public:
    Transform() {}
    Transform(glm::vec3 position, glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3 scale = glm::vec3(1.f))
        : position(position), rotation(rotation), scale(scale) {}

    glm::vec3 position = glm::vec3(0.f);
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::vec3 scale = glm::vec3(1.f);

    glm::mat4 matrix() const {
        return glm::scale(glm::translate(glm::mat4(1.f), position) * glm::mat4_cast(rotation), scale);
    }
};

}

#endif
