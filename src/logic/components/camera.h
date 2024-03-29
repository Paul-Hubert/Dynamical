#ifndef CAMERA_H
#define CAMERA_H

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "position.h"
#include "util/log.h"

namespace dy {

class Camera {
public:

    glm::vec3 getCenter() {
        return center;
    }

    glm::vec2 getSize() {
        return size;
    }
    
    float getRotation() {
        return rotation;
    }
    
    float getAngle() {
        return angle;
    }
    
    glm::vec2 getScreenSize() {
        return screen_size;
    }

    glm::mat4 getProjection() {
        if(projection_update) {
            projection_update = false;
            projection = createProjection();
        }
        return projection;
    }

    glm::mat4 getView() {
        if(view_update) {
            view_update = false;
            view = createView();
        }
        return view;
    }

    glm::mat4 createProjection();

    glm::mat4 createView();

    void setCenter(glm::vec3 center) {
        this->center = center;
        view_update = true;
    }

    void setScreenSize(glm::vec2 screen_size) {
        this->screen_size = screen_size;
        projection_update = true;
    }

    void setSize(glm::vec2 size) {
        this->size = size;
        projection_update = true;
    }
    
    void setRotation(float rotation) {
        this->rotation = rotation;
        view_update = true;
    }
    
    void setAngle(float angle) {
        this->angle = angle;
        view_update = true;
    }

    glm::vec2 fromWorldSpace(glm::vec3 position);
    
    glm::vec3 fromScreenSpace(glm::vec2 screen_position);
    
    glm::vec2 fromWorldSize(glm::vec2 world_size) {
        return world_size * screen_size / size;
    }
    
    glm::vec2 fromScreenSize(glm::vec2 pixel_size) {
        return pixel_size / screen_size * size;
    }

private:

    glm::vec3 center;
    glm::vec2 size;
    glm::vec2 screen_size;
    glm::mat4 projection;
    bool projection_update = true;
    glm::mat4 view;
    bool view_update = true;
    float rotation;
    float angle;
    
};

}

#endif
