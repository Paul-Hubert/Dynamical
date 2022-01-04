#ifndef DY_INPUT_H
#define DY_INPUT_H

#include "entt/entt.hpp"

#include "system.h"

namespace dy {

class InputSys : public System {
public:
    InputSys(entt::registry& reg);
    ~InputSys() override;
    const char* name() override {
        return "Input";
    }
    void tick(float dt) override;
};

}

#endif
