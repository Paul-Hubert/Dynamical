#ifndef EDITOR_CONTROL_H
#define EDITOR_CONTROL_H

#include "logic/systems/system.h"

class EditorControlSys : public System {
public:
    EditorControlSys(entt::registry& reg) : System(reg) {};
    void preinit() override;
    void init() override;
    void tick(float dt) override;
    void finish() override;
    const char* name() override { return "EditorControlSys"; };
};

#endif