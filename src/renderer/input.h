#ifndef DY_INPUT_H
#define DY_INPUT_H

#include "entt/entt.hpp"

class Input {
public:
    Input(entt::registry& reg);
    void poll();
private:
    entt::registry& reg;
};

#endif