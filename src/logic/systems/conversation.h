#ifndef CONVERSATIONSYS_H
#define CONVERSATIONSYS_H

#include "system.h"

namespace dy {

class ConversationSys : public System {
public:
    ConversationSys(entt::registry& reg) : System(reg) {};
    void tick(double dt) override;
    const char* name() override { return "ConversationSys"; };
};

}

#endif
