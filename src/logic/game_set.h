#ifndef GAME_SET_H
#define GAME_SET_H

#include <memory>
#include <vector>

#include "systems/system_set.h"
#include "entt/entt.hpp"

class Game;

class GameSet : public SystemSet {
public:
    GameSet(Game& game);
private:
    std::vector<std::unique_ptr<System>> owned_systems;
};

#endif
