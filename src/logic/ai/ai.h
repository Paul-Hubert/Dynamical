#ifndef AISYS_H
#define AISYS_H

#include "logic/systems/system.h"

class AISys : public System {
public:
    AISys(entt::registry& reg);
    ~AISys() override;
    const char* name() override {
        return "AI";
    }
    void tick(float dt) override;
};

#endif
