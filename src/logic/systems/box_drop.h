#ifndef DY_BOX_DROP_H
#define DY_BOX_DROP_H

#include "system.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace dy {

// World system (init) of the test scenes: drops `count` bodies on a centered grid above the ground, with the
// world RNG for the jitter, heights, sizes and orientations. Every capsule_every-th body is a capsule.
// params: { "count": 50, "spacing": 1.4, "height": [0.5, 2], "half_extent": [0.15, 0.3], "capsule_every": 0 }
class BoxDropSys : public System {
public:
    BoxDropSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "BoxDrop"; }
    void init() override;
    void tick(double dt) override {}

private:
    int count = 50;
    float spacing = 1.4f;
    glm::vec2 height = glm::vec2(0.5f, 2.f);
    glm::vec2 half_extent = glm::vec2(0.15f, 0.3f);
    int capsule_every = 0;
};

}

#endif
