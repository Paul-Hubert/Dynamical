#include "game_set.h"

#include <iostream>

#include "taskflow/taskflow.hpp"
#include "systems/system_list.h"
#include "renderer/marching_cubes/terrain.h"
#include "renderer/marching_cubes/marching_cubes.h"
#include "logic/components/settingsc.h"
#include "systems/physics.h"
#include "systems/uploader.h"

#include "logic/game.h"

#include "renderer/renderer.h"
#include "systems/ui.h"

#define MAKE_SYSTEM(TYPE, NAME) \
owned_systems.push_back(std::make_unique<TYPE>(reg)); \
auto NAME = owned_systems.back().get(); \
add(NAME);

GameSet::GameSet(Game& game) : SystemSet(game.reg) {
    
    entt::registry& reg = game.reg;
    
    reg.set<tf::Executor>();
    
    SettingsC& s = reg.ctx<SettingsC>();
    
    bool server = s.server_side;
    bool client = s.client_side;
    bool multiplayer = server || client;
    bool singleplayer = !multiplayer;
    bool user = client || singleplayer;
    
    if(user) {
        MAKE_SYSTEM(InputSys, input)
        MAKE_SYSTEM(DebugSys, debug)
        add(game.ui.get());
        MAKE_SYSTEM(PlayerControlSys, control)
    }
    
    MAKE_SYSTEM(PhysicsSys, physics)
    
    if(user) {
        MAKE_SYSTEM(UploaderSys, uploader)
        add(game.renderer.get());
    }
    
}

