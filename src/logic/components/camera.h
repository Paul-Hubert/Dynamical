#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace dy {

// Free flying camera of the desktop window, a context variable driven by PlayerControlSys.
// World is Y up, yaw 0 looks toward -Z.
class Camera {
public:

    glm::vec3 position = glm::vec3(0.f, 150.f, 0.f);
    float yaw = 0.f;
    float pitch = -0.4f;

    float fov = glm::radians(70.f);
    float near_plane = 0.1f;
    float far_plane = 3000.f;

    bool sprinting = false;

    glm::vec3 forward() const {
        return glm::vec3(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
    }

    glm::vec3 right() const {
        return glm::vec3(std::cos(yaw), 0.f, -std::sin(yaw));
    }

    glm::mat4 getView() const {
        return glm::lookAt(position, position + forward(), glm::vec3(0.f, 1.f, 0.f));
    }

    // Vulkan clip space : depth in [0, 1] and Y pointing down
    glm::mat4 getProjection(float aspect) const {
        glm::mat4 projection = glm::perspectiveRH_ZO(fov, aspect, near_plane, far_plane);
        projection[1][1] *= -1.f;
        return projection;
    }

};

}

#endif
