#ifndef GAME_H
#define GAME_H

#include <memory>

#include "entt/entt.hpp"

#include "systems/ui.h"
#include "renderer/renderer.h"

#include "systems/settings.h"

class SystemSet;

class Game {
public:
    Game(int argc, char** argv);
    void init();
    void start();
    ~Game();

    Renderer renderer;

    entt::registry reg;
    
    SettingSys settings;
    
    std::unique_ptr<SystemSet> set;
    
};

#endif
