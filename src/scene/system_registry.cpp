#include "system_registry.h"

#include <map>

#include "component_registry.h"

#include "logic/components/model_ref.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/systems/chunk_systems.h"
#include "physics/physics_sys.h"

using namespace dy;

namespace {

std::map<std::string, SystemInfo>& systems() {
    static std::map<std::string, SystemInfo> map;
    return map;
}

const char* const motion_names[] = { "static", "dynamic", "kinematic" };
const char* const shape_names[] = { "box", "sphere" };

void registerSimComponents() {

    registerComponent("Transform", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            Transform t;
            JsonFields f(j);
            f.read("position", t.position);
            f.read("rotation", t.rotation);
            f.read("scale", t.scale);
            f.finish();
            reg.emplace_or_replace<Transform>(e, t);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& t = reg.get<Transform>(e);
            return nlohmann::json { { "position", toJson(t.position) }, { "rotation", toJson(t.rotation) }, { "scale", toJson(t.scale) } };
        }
    });

    registerComponent("RigidBody", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            RigidBody b;
            JsonFields f(j);
            f.readEnum("motion", b.motion, motion_names);
            f.readEnum("shape", b.shape, shape_names);
            f.read("half_extent", b.half_extent);
            f.read("offset", b.offset);
            f.read("friction", b.friction);
            f.read("restitution", b.restitution);
            f.read("initial_velocity", b.initial_velocity);
            f.read("initial_angular_velocity", b.initial_angular_velocity);
            f.finish();
            reg.emplace_or_replace<RigidBody>(e, b);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& b = reg.get<RigidBody>(e);
            return nlohmann::json { { "motion", motion_names[(int) b.motion] }, { "shape", shape_names[(int) b.shape] },
                { "half_extent", toJson(b.half_extent) }, { "offset", toJson(b.offset) }, { "friction", b.friction },
                { "restitution", b.restitution }, { "initial_velocity", toJson(b.initial_velocity) },
                { "initial_angular_velocity", toJson(b.initial_angular_velocity) } };
        }
    });

    registerComponent("ModelRef", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            ModelRef m;
            JsonFields f(j);
            f.read("path", m.path);
            f.read("color", m.color);
            f.finish();
            if(m.path.empty()) throw ComponentError("/path", "a model path is required");
            reg.emplace_or_replace<ModelRef>(e, m);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& m = reg.get<ModelRef>(e);
            return nlohmann::json { { "path", m.path }, { "color", toJson(m.color) } };
        }
    });

}

}

void dy::registerSystem(const std::string& name, SystemInfo info) {
    systems()[name] = std::move(info);
}

const SystemInfo* dy::findSystem(const std::string& name) {
    auto it = systems().find(name);
    return it != systems().end() ? &it->second : nullptr;
}

void dy::registerSimSystems() {

    registerSystem("ChunkManager", systemInfo<ChunkManagerSys>(SystemScope::Host));
    registerSystem("ChunkGenerator", systemInfo<ChunkGeneratorSys>(SystemScope::Host));
    registerSystem("RigidBody", systemInfo<RigidBodySys>(SystemScope::Both));
    registerSystem("TerrainColliders", systemInfo<TerrainCollidersSys>(SystemScope::Host));
    registerSystem("Physics", systemInfo<PhysicsSys>(SystemScope::Host));
    registerSystem("PhysicsStep", systemInfo<PhysicsStepSys>(SystemScope::World));

    registerSimComponents();

}
