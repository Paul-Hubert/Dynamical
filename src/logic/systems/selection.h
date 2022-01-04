#ifndef SELECTIONSYS_H
#define SELECTIONSYS_H

#include "system.h"

namespace dy {
    
class SelectionSys : public System {
public:
    SelectionSys(entt::registry& reg) : System(reg) {};
    void preinit() override;
    void tick(float dt) override;
    const char* name() override { return "SelectionSys"; };
private:
    void select(entt::entity entity);
    void unselect();
};

}

#endif
