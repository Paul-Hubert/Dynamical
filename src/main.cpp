#include "logic/game.h"

#include <memory>
#include "extra/optick/optick.h"

int main(int argc, char **argv) {
    
    //OPTICK_APP("People");

    auto game = std::make_unique<dy::Game>(argc, argv);

    game->start();
    
    return 0;
    
}
