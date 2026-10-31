#ifndef DY_SPAWN_H
#define DY_SPAWN_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace dy {

// Dynamic box (resources/models/box.gltf) with a Jolt body, returns entt::null if the model is missing
entt::entity spawnBox(entt::registry& reg, glm::vec3 position, float size, glm::vec4 color, glm::vec3 velocity = glm::vec3(0.f));

}

#endif
