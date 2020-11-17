#ifndef CAMERA_H
#define CAMERA_H

#include "entt/entt.hpp"
#include "glm/glm.hpp"

constexpr float render_distance = 1000.f;
constexpr float render_min = 0.1f;
constexpr float render_fov = 70.f;

class Context;

class Camera {
public:
    Camera(entt::registry& reg, Context& ctx);
    void setup(int width, int height);
    void update();
    ~Camera();
    
    glm::mat4& getProjection();
    glm::mat4& getView();
    glm::mat4& getViewProjection();
    
    glm::vec3 getViewPosition();
    
private:
    entt::registry& reg;
    Context& ctx;
    
    glm::vec3 position;
    
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 viewProjection;
    
};

#endif
