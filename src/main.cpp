#include "view/game.h"
#include "view/register_view.h"
#include "scene/system_registry.h"

#include <filesystem>
#include <memory>
#include <SDL.h>
#include "extra/optick/optick.h"

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

    //OPTICK_APP("People");

    find_resources();

    dy::registerSimSystems();
    dy::registerViewSystems();

    auto game = std::make_unique<dy::Game>(argc, argv);

    return game->start();

}
