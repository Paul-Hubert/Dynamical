#include "register_view.h"

#include "scene/component_registry.h"
#include "scene/system_registry.h"

#include "renderer/model/model_manager.h"
#include "renderer/terrain/terrain_sys.h"
#include "view/components/model_renderable.h"
#include "view/systems/input.h"
#include "view/systems/system_list.h"
#include "view/systems/ui.h"
#include "view/world_stepper.h"
#include "view/sphere_view.h"
#include "view/roller_player.h"
#include "view/world_view.h"
#include "view/fixed_camera.h"
#include "view/camera_walk.h"
#include "view/walker_view.h"
#include "view/gait_view.h"

using namespace dy;

void dy::registerViewSystems() {

    registerSystem("Input", systemInfo<InputSys>(SystemScope::Host, true));
    registerSystem("UI", systemInfo<UISys>(SystemScope::Host, true));
    registerSystem("Time", systemInfo<TimeSys>(SystemScope::Host, true));
    registerSystem("PlayerControl", systemInfo<PlayerControlSys>(SystemScope::Host, true));
    registerSystem("VRControl", systemInfo<VRControlSys>(SystemScope::Host, true));
    registerSystem("DebugMenu", systemInfo<DebugMenuSys>(SystemScope::Host, true));
    registerSystem("BoxRing", systemInfo<BoxRingSys>(SystemScope::Host, true));
    registerSystem("ModelLineup", systemInfo<ModelLineupSys>(SystemScope::Host, true));
    registerSystem("BoxThrower", systemInfo<BoxThrowerSys>(SystemScope::Host, true));
    registerSystem("Terrain", systemInfo<TerrainSys>(SystemScope::Host, true));
    registerSystem("WorldStepper", systemInfo<WorldStepperSys>(SystemScope::Host, true));
    registerSystem("FixedCamera", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<FixedCameraSys>(reg, params); },
        SystemScope::Host, true });
    registerSystem("CameraWalk", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<CameraWalkSys>(reg, params); },
        SystemScope::Host, true });
    registerSystem("SphereView", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<SphereViewSys>(reg, params); },
        SystemScope::Host, true });
    registerSystem("VRRoller", { [](entt::registry& reg, const nlohmann::json& params) {
        return std::make_unique<RollerPlayerSys>(reg, params, RollerPlayerSys::Player::VR); }, SystemScope::Host, true });
    registerSystem("DesktopRoller", { [](entt::registry& reg, const nlohmann::json& params) {
        return std::make_unique<RollerPlayerSys>(reg, params, RollerPlayerSys::Player::Desktop); }, SystemScope::Host, true });
    registerSystem("WalkerView", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<WalkerViewSys>(reg, params); }, SystemScope::Host, true });
    registerSystem("GaitView", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<GaitViewSys>(reg, params); }, SystemScope::Host, true });
    registerSystem("WorldView", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<WorldViewSys>(reg, params); },
        SystemScope::Host, true });

    // Needs the renderer's ModelManager: only valid once the renderer exists
    registerComponent("ModelRenderable", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            std::string path;
            glm::vec4 tint(1.f);
            JsonFields f(j);
            f.read("path", path);
            f.read("tint", tint);
            f.finish();
            auto* models = reg.ctx().find<ModelManager>();
            if(!models) throw ComponentError("", "ModelRenderable needs the renderer");
            entt::entity model = models->load("./resources/" + path);
            if(model == entt::null) throw ComponentError("/path", "cannot load model " + path);
            reg.emplace_or_replace<ModelRenderable>(e, model, tint);
        },
        [](const entt::registry& reg, entt::entity e) {
            return nlohmann::json { { "tint", toJson(reg.get<ModelRenderable>(e).tint) } };
        }
    });

}
