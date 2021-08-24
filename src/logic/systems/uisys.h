#ifndef UISYS_H
#define UISYS_H

#include "system.h"

#include "entt/entt.hpp"

class UISys : public System
{
public:
    UISys(entt::registry& reg);

    ~UISys() override;

    const char* name() override {
        return "UI";
    }

    void tick(float dt) override;

    void init() override;

    void preinit() override;

    void finish() override;

};

#endif // UISYS_H
