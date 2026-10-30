#ifndef DY_VR_COMPONENTS_H
#define DY_VR_COMPONENTS_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace dy {

// Where the VR play area (OpenXR stage space) sits in the world. Context variable.
struct VRRig {
    glm::vec3 position = glm::vec3(0.f);
    float yaw = 0.f;

    glm::mat4 transform() const {
        return glm::rotate(glm::translate(glm::mat4(1.f), position), yaw, glm::vec3(0.f, 1.f, 0.f));
    }
};

struct VRPose {
    bool valid = false;
    glm::vec3 position = glm::vec3(0.f);
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);

    glm::mat4 matrix() const {
        return glm::translate(glm::mat4(1.f), position) * glm::mat4_cast(rotation);
    }
};

// Written by the VR renderer after xrWaitFrame, in stage space. Context variable.
struct VRInput {
    bool enabled = false;         // a headset is connected
    bool session_running = false;
    bool focused = false;

    VRPose head;

    struct Hand {
        VRPose pose;
        float trigger = 0.f;
        float grip = 0.f;
        glm::vec2 thumbstick = glm::vec2(0.f);
        bool trigger_pressed = false; // rising edge this frame
    };
    Hand hands[2];

    static constexpr int left = 0;
    static constexpr int right = 1;
};

}

#endif
