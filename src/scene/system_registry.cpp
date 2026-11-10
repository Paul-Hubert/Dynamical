#include "system_registry.h"

#include <map>

#include "component_registry.h"

#include "logic/agents/gait.h"
#include "logic/agents/ground_field.h"
#include "logic/agents/humanbot.h"
#include "logic/agents/ik.h"
#include "logic/agents/sphere_roller.h"
#include "logic/agents/walk_routes.h"
#include "logic/components/model_ref.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/systems/box_drop.h"
#include "logic/systems/chunk_systems.h"
#include "logic/vegetation/vegetation_scatter.h"
#include "physics/physics_sys.h"

using namespace dy;

namespace {

std::map<std::string, SystemInfo>& systems() {
    static std::map<std::string, SystemInfo> map;
    return map;
}

const char* const motion_names[] = { "static", "dynamic", "kinematic" };
const char* const shape_names[] = { "box", "sphere", "capsule" };

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
            f.read("angular_damping", b.angular_damping);
            f.read("mass", b.mass);
            f.read("initial_velocity", b.initial_velocity);
            f.read("initial_angular_velocity", b.initial_angular_velocity);
            f.finish();
            reg.emplace_or_replace<RigidBody>(e, b);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& b = reg.get<RigidBody>(e);
            return nlohmann::json { { "motion", motion_names[(int) b.motion] }, { "shape", shape_names[(int) b.shape] },
                { "half_extent", toJson(b.half_extent) }, { "offset", toJson(b.offset) }, { "friction", b.friction },
                { "restitution", b.restitution }, { "angular_damping", b.angular_damping }, { "mass", b.mass }, { "initial_velocity", toJson(b.initial_velocity) },
                { "initial_angular_velocity", toJson(b.initial_angular_velocity) } };
        }
    });

    registerComponent("Ground", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            Ground g;
            JsonFields f(j);
            f.read("size", g.size);
            f.read("top_y", g.top_y);
            f.read("friction", g.friction);
            f.read("restitution", g.restitution);
            f.finish();
            if(g.size.x <= 0.f || g.size.y <= 0.f || g.size.z <= 0.f) throw ComponentError("/size", "sizes must be positive");
            reg.emplace_or_replace<Ground>(e, g);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& g = reg.get<Ground>(e);
            return nlohmann::json { { "size", toJson(g.size) }, { "top_y", g.top_y }, { "friction", g.friction }, { "restitution", g.restitution } };
        }
    });

    registerComponent("HeightField", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            HeightField h;
            JsonFields f(j);
            int seed = 0;
            f.read("samples", h.samples);
            f.read("spacing", h.spacing);
            f.read("waves", h.waves);
            f.read("wavelength", h.wavelength);
            f.read("wave_slope", h.wave_slope);
            f.read("friction", h.friction);
            f.read("restitution", h.restitution);
            f.read("seed", seed);
            f.finish();
            if(h.samples < 8 || (h.samples & (h.samples - 1)) != 0) throw ComponentError("/samples", "a power of 2, at least 8");
            if(h.spacing <= 0.f) throw ComponentError("/spacing", "must be positive");
            if(h.waves < 0) throw ComponentError("/waves", "must not be negative");
            if(h.wavelength.x <= 0.f || h.wavelength.y < h.wavelength.x) throw ComponentError("/wavelength", "expected 0 < min <= max");
            if(seed < 0) throw ComponentError("/seed", "must not be negative");
            h.seed = (uint32_t) seed;
            reg.emplace_or_replace<HeightField>(e, h);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& h = reg.get<HeightField>(e);
            return nlohmann::json { { "samples", h.samples }, { "spacing", h.spacing }, { "waves", h.waves },
                { "wavelength", { h.wavelength.x, h.wavelength.y } }, { "wave_slope", h.wave_slope }, { "friction", h.friction },
                { "restitution", h.restitution }, { "seed", h.seed } };
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
    registerSystem("WorldTerrain", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<WorldTerrainSys>(reg, params); },
        SystemScope::World });
    registerSystem("BoxDrop", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<BoxDropSys>(reg, params); },
        SystemScope::World });

    registerSimComponents();
    registerSphereRoller();
    registerHumanBot();
    registerIK();
    registerGroundField();
    registerGait();
    registerWalkRoutes();
    registerVegetation();

}
