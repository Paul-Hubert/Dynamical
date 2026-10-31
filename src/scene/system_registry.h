#ifndef DY_SYSTEM_REGISTRY_H
#define DY_SYSTEM_REGISTRY_H

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>

#include "logic/systems/system.h"

namespace dy {

enum class SystemScope : uint8_t { Host, World, Both };

using SystemFactory = std::function<std::unique_ptr<System>(entt::registry&, const nlohmann::json& params)>;

struct SystemInfo {
    SystemFactory make;
    SystemScope scope = SystemScope::Host;
    bool needs_renderer = false;
};

// Scenes name systems by registry name, which is independent of System::name()
// (DEFINE_SYSTEM's TimeSys is registered as Time). Registering a name twice replaces it.
void registerSystem(const std::string& name, SystemInfo info);
const SystemInfo* findSystem(const std::string& name);

template<typename T>
SystemInfo systemInfo(SystemScope scope, bool needs_renderer = false) {
    return { [](entt::registry& reg, const nlohmann::json&) { return std::make_unique<T>(reg); }, scope, needs_renderer };
}

// The library's systems and components. Called once from each main and from the test main
void registerSimSystems();

}

#endif
