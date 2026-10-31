#ifndef DY_MODEL_RENDERABLE_H
#define DY_MODEL_RENDERABLE_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace dy {

// Draws the ModelC of another entity (see ModelManager) at this entity's Transform
class ModelRenderable {
public:
    ModelRenderable(entt::entity model, glm::vec4 tint = glm::vec4(1.f)) : model(model), tint(tint) {}
    entt::entity model;
    glm::vec4 tint;
};

}

#endif
