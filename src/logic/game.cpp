#include "game.h"

#include <iostream>
#include <taskflow/taskflow.hpp>
#include "extra/optick/optick.h"

#include "renderer/renderer.h"
#include "renderer/terrain/terrain_sys.h"
#include "physics/physics_sys.h"
#include "systems/system_set.h"

#include "systems/system_list.h"
#include "systems/chunk_systems.h"
#include "systems/input.h"
#include "systems/ui.h"

#include "util/entt_util.h"

#include "logic/settings/settings.h"
#include "scene/run_args.h"

#include "logic/components/frame_clock.h"
#include "logic/components/input.h"

#include "util/log.h"

#include <algorithm>
#include <chrono>

using namespace std::chrono_literals;

using namespace dy;

Game::Game(int argc, char** argv) {
    registry = std::make_unique<entt::registry>();
    entt::registry& reg = *registry;
    reg.ctx().emplace<Settings>(argc, argv);
    reg.ctx().emplace<tf::Executor>();

    // Settings parses its own keys from the same arguments, anything else unknown is only a warning here
    static const char* settings_keys[] = { "window_width", "window_height", "fullscreen", "fps_max", "username",
        "server_side", "client_side", "vr_mode", "view_distance" };
    auto parsed = parseRunArgs(argc, argv);
    for(auto& key : parsed.unknown) {
        if(std::find(std::begin(settings_keys), std::end(settings_keys), key) == std::end(settings_keys)) {
            dy::log(dy::warning) << "Unknown argument " << key << "\n";
        }
    }
    for(auto& error : parsed.errors) {
        dy::log(dy::warning) << "Argument ignored, " << error << "\n";
    }
    reg.ctx().emplace<RunArgs>(parsed.args);

    // Captures and probes: a 960 x 540 window, no headset. Not saved to the config file
    if(parsed.args.deterministic()) {
        auto& settings = reg.ctx().get<Settings>();
        settings.window_width = 960;
        settings.window_height = 540;
        settings.fullscreen = false;
        settings.vr_mode = 0;
    }
    reg.ctx().emplace<FrameClock>();
}

void Game::start() {

    entt::registry& reg = *registry;

    // Creates the Vulkan context, OpenXR session, both render targets and the scene passes
    renderer = std::make_unique<Renderer>(reg);

    set = std::make_unique<SystemSet>(reg);

    set->pre_add<InputSys>();
    set->pre_add<UISys>();
    set->pre_add<TimeSys>();

    set->pre_add<PlayerControlSys>();
    set->pre_add<VRControlSys>();
    set->pre_add<DebugMenuSys>();

    set->pre_add<ChunkManagerSys>();
    set->pre_add<ChunkGeneratorSys>();
    set->pre_add<SceneSys>();

    // Fixed step Jolt simulation, writes the Transforms of dynamic bodies before rendering
    set->pre_add<PhysicsSys>();

    // Records the marching cubes dispatches into this frame's compute command buffer
    set->post_add<TerrainSys>();


    // preinit

    renderer->preinit();

    set->preinit();


    // init

    set->init();

    renderer->init();

    Input& input = reg.ctx().get<Input>();
    const RunArgs& args = reg.ctx().get<RunArgs>();
    FrameClock& frame_clock = reg.ctx().get<FrameClock>();

    // start loop

    using clock = std::chrono::high_resolution_clock;

    auto start = clock::now();

    bool running = true;
    while(running) {

        OPTICK_FRAME("Main");

        renderer->prepare();

        if (input.on[Input::EXIT]) {
            running = false;
            input.on.set(Input::EXIT, false);
        }

        double dt = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - start).count() / 1000000000.0;
        start = clock::now();
        if(args.deterministic()) dt = args.fixed_frame_dt;

        frame_clock.dt = dt;
        frame_clock.time += dt;
        frame_clock.frame++;

        set->pre_tick(dt);

        set->post_tick(dt);

        renderer->render();

    }

}

Game::~Game() {

    if(set) set->finish();
    if(renderer) renderer->finish();

    // Systems own GPU resources (terrain buffers) : destroy them while the device exists
    set.reset();
    renderer.reset();

}
