#include "logic/game.h"

#include <memory>
#include "extra/optick/optick.h"
#include "util/log.h"

int main(int argc, char **argv) {

    //OPTICK_APP("People");

    dy::set_log_level(dy::Level::trace);

    auto game = std::make_unique<dy::Game>(argc, argv);

    game->start();
    
    return 0;
    
}
