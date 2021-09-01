#ifndef GAME_H
#define GAME_H

#include <memory>

#include "entt/entt.hpp"

namespace dy {

class SystemSet;
class Renderer;

class Game {
public:
    Game(int argc, char** argv);
    void start();
    ~Game();

    std::unique_ptr<entt::registry> registry;

    std::unique_ptr<Renderer> renderer;

    std::unique_ptr<SystemSet> set;

};

}

#endif
