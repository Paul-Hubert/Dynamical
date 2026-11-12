#include "game.h"

#include <iostream>
#include <taskflow/taskflow.hpp>
#include "extra/optick/optick.h"

#include "renderer/renderer.h"
#include "logic/systems/system_set.h"
#include "scene/scene_def.h"
#include "scene/system_registry.h"
#include "scene/world.h"

#include "util/entt_util.h"

#include "logic/settings/settings.h"
#include "scene/run_args.h"

#include "logic/components/frame_clock.h"
#include "view/components/input.h"

#include "util/log.h"
#include "scene/probe.h"
#include "view/capture.h"

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
        "server_side", "client_side", "vr_mode", "view_distance", "grass_kind", "ground_relief" };
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

    // Captures and probes: a 960 x 540 window, no headset. Not saved to the config file.
    // With gpu_times the window keeps the size that was asked for: the passes are timed at that size.
    // With capture_window too: the captures of a video are that large
    if(parsed.args.deterministic()) {
        auto& settings = reg.ctx().get<Settings>();
        if(!parsed.args.gpu_times && !parsed.args.capture_window) {
            settings.window_width = 960;
            settings.window_height = 540;
        }
        settings.fullscreen = false;
        settings.vr_mode = 0;
    }
    reg.ctx().emplace<FrameClock>();
}

int Game::start() {

    entt::registry& reg = *registry;

    // The scene lists the passes and the host systems (base: master's Game::start)
    SceneDef scene;
    try {
        const RunArgs& args = reg.ctx().get<RunArgs>();
        std::string name = args.scene;
        auto vars = args.vars;
        // A capture script names its scene; the command line may repeat it, not contradict it
        if(!args.capture.empty()) {
            std::string script_scene;
            std::vector<std::pair<std::string, std::string>> script_vars;
            if(Capture::readScene(args.capture, script_scene, script_vars)) {
                if(!name.empty() && name != script_scene) {
                    dy::log(dy::error) << "scene=" << name << " but the capture script runs " << script_scene << "\n";
                    return 1;
                }
                name = script_scene;
                vars.insert(vars.begin(), script_vars.begin(), script_vars.end());
            }
        }
        scene = loadScene(name.empty() ? "base" : name, vars, SceneLoadOptions {});
    } catch(const SceneError& e) {
        dy::log(dy::error) << "Scene: " << e.what() << "\n";
        return 1;
    }
    if(!scene.renderer) {
        dy::log(dy::error) << "Scene " << scene.file << " has no renderer block: run it with dynamical_train\n";
        return 1;
    }

    // Creates the Vulkan context, OpenXR session, both render targets and the scene passes
    if(!scene.renderer->vr) reg.ctx().get<Settings>().vr_mode = 0;
    renderer = std::make_unique<Renderer>(reg, *scene.renderer);

    set = std::make_unique<SystemSet>(reg);

    for(auto& e : scene.host_pre) set->add(SystemSet::List::Pre, findSystem(e.name)->make(reg, e.params), e.phase, e.name);
    for(auto& e : scene.host_post) set->add(SystemSet::List::Post, findSystem(e.name)->make(reg, e.params), e.phase, e.name);

    // The scene's worlds, stepped by WorldStepper and drawn by WorldView
    Worlds* worlds = nullptr;
    if(!scene.worlds.empty()) {
        worlds = &reg.ctx().emplace<Worlds>();
        try {
            worlds->create(scene, (uint64_t) reg.ctx().get<RunArgs>().seed);
        } catch(const SceneError& e) {
            dy::log(dy::error) << "Scene: " << e.what() << "\n";
            return 1;
        }
    }


    // preinit

    renderer->preinit();

    set->preinit();

    if(worlds) worlds->preinit();


    // init

    set->init();

    if(worlds) worlds->init();

    renderer->init();

    Input& input = reg.ctx().get<Input>();
    const RunArgs& args = reg.ctx().get<RunArgs>();
    FrameClock& frame_clock = reg.ctx().get<FrameClock>();

    std::unique_ptr<Capture> capture;
    if(!args.capture.empty()) {
        capture = std::make_unique<Capture>(reg, args);
        if(capture->failed()) {
            dy::log(dy::error) << capture->error << "\n";
            return 1;
        }
    }
    std::vector<uint8_t> pixels;

    std::unique_ptr<Probe> probe;
    if(!args.probe.empty()) {
        probe = std::make_unique<Probe>(args);
        if(probe->failed()) {
            dy::log(dy::error) << probe->error << "\n";
            return 1;
        }
    }

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

        if(capture) capture->beginFrame(frame_clock.frame);

        set->pre_tick(dt);

        set->post_tick(dt);

        if(probe) probe->sample(reg, frame_clock.frame);

        bool shoot = capture && capture->due(frame_clock.frame) && renderer->requestCapture();

        renderer->render();

        if(shoot) {
            uint32_t width, height;
            if(renderer->takeCapture(pixels, width, height)) capture->save(pixels, width, height);
            else capture->error = "frame readback failed";
        }

        if(capture) {
            if(capture->failed()) {
                dy::log(dy::error) << capture->error << "\n";
                return 1;
            }
            if(capture->done() && args.exit_after_capture) running = false;
        }
        if(args.ticks > 0 && frame_clock.frame >= (uint64_t) args.ticks) running = false;

    }

    return 0;

}

Game::~Game() {

    if(auto* worlds = registry->ctx().find<Worlds>()) worlds->finish();
    if(set) set->finish();
    if(renderer) renderer->finish();

    // Systems own GPU resources (terrain buffers) : destroy them while the device exists
    set.reset();
    renderer.reset();

}
