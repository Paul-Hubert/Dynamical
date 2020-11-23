#ifndef GAME_H
#define GAME_H

#include <memory>

#include "entt/entt.hpp"

#include "systems/settings.h"

class UISys;
class Renderer;
class SystemSet;

class Game {
public:
    Game(int argc, char** argv);
    void init();
    void start();
    ~Game();
    
    std::unique_ptr<UISys> ui;

    std::unique_ptr<Renderer> renderer;

    entt::registry reg;
    
    SettingSys settings;
    
    std::unique_ptr<SystemSet> set;
    
};

#endif
