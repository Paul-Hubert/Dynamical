#include "logic/game.h"

#include <memory>

int main(int argc, char **argv) {

    auto game = std::make_unique<Game>(argc, argv);

    game->start();
    
    return 0;
    
}
