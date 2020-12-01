#ifndef DY_VR_INPUT_H
#define DY_VR_INPUT_H

#include "entt/entt.hpp"

class VRInput {
public:
    VRInput(entt::registry& reg);
    void poll();
    void update();
private:
    entt::registry& reg;
};

#endif