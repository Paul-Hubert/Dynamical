#ifndef GAME_H
#define GAME_H

#include <memory>

#include "entt/entt.hpp"

class SystemSet;
class Renderer;
class PhysicsSys;

class Game {
public:
    Game(int argc, char** argv);
    void start();
    ~Game();

    std::unique_ptr<Renderer> renderer;

    entt::registry reg;
    
    std::unique_ptr<SystemSet> pre;

    std::unique_ptr<PhysicsSys> physics;

    std::unique_ptr<SystemSet> post;

};

#endif
