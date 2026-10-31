#include "test_systems.h"

#include "scene/system_registry.h"

using namespace dy;
using namespace dy::test;

void StubSys::tick(double dt) {
    if(auto* log = reg.ctx().find<CallLog>()) log->calls.push_back(label + "@" + std::to_string(ticks));
    ticks++;
}

namespace {

SystemInfo stub(const std::string& name, SystemScope scope, bool renderer = false) {
    return { [name](entt::registry& reg, const nlohmann::json&) { return std::make_unique<StubSys>(reg, name); }, scope, renderer };
}

}

void dy::test::registerTestSystems() {
    for(const char* name : { "TestHost", "TestHost2", "TestInput", "TestUI" }) registerSystem(name, stub(name, SystemScope::Host));
    registerSystem("TestWorld", stub("TestWorld", SystemScope::World));
    registerSystem("TestBoth", stub("TestBoth", SystemScope::Both));
    registerSystem("TestRender", stub("TestRender", SystemScope::Host, true));
}

void dy::test::registerViewSystemStubs() {
    for(const char* name : { "Input", "UI", "Time", "PlayerControl", "VRControl", "DebugMenu", "BoxRing", "ModelLineup", "BoxThrower", "Terrain" }) {
        registerSystem(name, stub(name, SystemScope::Host, true));
    }
}
