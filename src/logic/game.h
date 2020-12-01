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

    std::unique_ptr<entt::registry> registry;

    std::unique_ptr<PhysicsSys> physics;

    std::unique_ptr<SystemSet> pre_sets;

    std::unique_ptr<SystemSet> update_sets;

    std::unique_ptr<SystemSet> post_sets;

};

#endif
