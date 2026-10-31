#ifndef DY_TEST_SYSTEMS_H
#define DY_TEST_SYSTEMS_H

#include <string>
#include <vector>

#include "logic/systems/system.h"

namespace dy::test {

// Stub systems for the loader tests: TestHost, TestHost2, TestInput, TestUI (host), TestWorld (world),
// TestBoth (both), TestRender (host, needs the renderer). Each tick appends "<name>@<tick>" to the log
void registerTestSystems();

// Registry names of the view systems (registerViewSystems() in dynamical), host scope, needing the renderer:
// lets the tests resolve rendered scenes such as base without linking the viewer
void registerViewSystemStubs();

class StubSys : public System {
public:
    StubSys(entt::registry& reg, std::string label) : System(reg), label(std::move(label)) {}
    void tick(double dt) override;
    const char* name() override { return label.c_str(); }
    std::string label;
    int ticks = 0;
};

// Call log shared by the stubs of a registry (context variable)
struct CallLog {
    std::vector<std::string> calls;
};

}

#endif
