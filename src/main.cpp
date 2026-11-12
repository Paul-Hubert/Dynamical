#include "view/game.h"
#include "view/register_view.h"
#include "scene/system_registry.h"
#include "physics/jolt_runtime.h"
#ifdef DYNAMICAL_ONNX
#include "onnx/policy_systems.h"
#endif

#include <filesystem>
#include <memory>
#include <SDL.h>
#include "scene/run_args.h"
#include "util/profile.h"

// Resources and the config file are loaded relative to the working directory. When the exe is
// started from elsewhere (double clicked in build/Debug), move to the closest parent of the exe
// that holds a resources folder.
static void find_resources() {
    namespace fs = std::filesystem;
    if(fs::exists("resources"))
        return;

    char* base = SDL_GetBasePath();
    if(!base)
        return;
    fs::path dir(reinterpret_cast<const char8_t*>(base));
    SDL_free(base);

    std::error_code error;
    for(int i = 0; i < 4 && !dir.empty(); i++, dir = dir.parent_path()) {
        if(fs::exists(dir / "resources", error)) {
            fs::current_path(dir, error);
            return;
        }
        if(dir == dir.parent_path())
            break;
    }
}

int main(int argc, char **argv) {

    // profile=true : the profiler runs from here to the end, a Tracy profiler connects when it wants (docs/profiling.md)
    if(dy::parseRunArgs(argc, argv).args.profile) dy::profile::start();

    find_resources();

    dy::JoltRuntime jolt;
    dy::registerSimSystems();
    dy::registerViewSystems();
#ifdef DYNAMICAL_ONNX
    dy::registerPolicySystems();
#endif

    int code;
    {
        auto game = std::make_unique<dy::Game>(argc, argv);
        code = game->start();
    }
    dy::profile::stop();
    return code;

}
