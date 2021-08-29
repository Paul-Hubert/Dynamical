#ifndef SELECTIONC_H
#define SELECTIONC_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

class SelectionC {
public:
    entt::entity entity = entt::null;
    glm::vec4 color;
};

#endif
