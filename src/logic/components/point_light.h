#ifndef DY_POINT_LIGHT_H
#define DY_POINT_LIGHT_H

#include <glm/glm.hpp>

namespace dy {

// A torch, a fire : next to a Transform. The few nearest to the viewers light the scene (the Lights of the renderer)
struct PointLight {
    glm::vec3 color = glm::vec3(1.f, 0.62f, 0.3f);
    float intensity = 12.f;      // light on a surface facing it, one metre away
    float radius = 18.f;        // metres : nothing is lit beyond
};

}

#endif
