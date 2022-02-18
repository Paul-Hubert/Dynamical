#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "position.h"

namespace dy {

class Camera {
public:

    glm::vec3 getCenter() {
        return center;
    }

    glm::vec2 getSize() {
        return size;
    }

    glm::vec2 getCorner() {
        return glm::vec2(center.x, center.y) - size / 2.f;
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

    void setScreenSize(glm::vec3 screen_size) {
        this->screen_size = screen_size;
        projection_update = true;
    }

    void setSize(glm::vec2 size) {
        this->size = size;
        projection_update = true;
    }

    glm::vec2 fromWorldSpace(glm::vec3 position) {
        return glm::project(position, view, projection, glm::vec4(screen_size.x, screen_size.y, 1, 1));
    }
    
    glm::vec3 fromScreenSpace(glm::vec2 screen_position) {
        return glm::unProject(glm::vec3(screen_position, 1), view, projection, glm::vec4(screen_size.x, screen_size.y, 1, 1));
    }
    
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
    //glm::vec2 corner;
    //glm::vec2 size;
    
};

}

#endif
