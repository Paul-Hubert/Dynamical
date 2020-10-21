#include "game_set.h"

#include <iostream>

#include "taskflow/taskflow.hpp"
#include "system_list.h"
#include "renderer/marching_cubes/terrain.h"
#include "renderer/marching_cubes/marching_cubes.h"
#include "logic/components/settingsc.h"
#include "client_network.h"
#include "server_network.h"
#include "physics.h"

#include "logic/game.h"

#include "renderer/renderer.h"
#include "ui.h"

#define MAKE_SYSTEM(TYPE, NAME) \
owned_systems.push_back(std::make_unique<TYPE>(reg)); \
auto NAME = owned_systems.back().get(); \
add(NAME);

GameSet::GameSet(Game& game) : SystemSet(game.reg) {
    
    entt::registry& reg = game.reg;
    
    reg.set<tf::Executor>();
    
    SettingsC& s = reg.ctx<SettingsC>();
    
#ifndef NDEBUG
    if(s.server_side) {
        Util::log(Util::INFO) << "server_side" << std::endl;
    } else if(s.client_side) {
        Util::log(Util::INFO) << "client_side" << std::endl;
    } else {
        Util::log(Util::INFO) << "single_player" << std::endl;
    }
#endif
    bool server = s.server_side;
    bool client = s.client_side;
    bool multiplayer = server || client;
    bool singleplayer = !multiplayer;
    bool user = client || singleplayer;
    
    if(user) {
        MAKE_SYSTEM(InputSys, input)
        MAKE_SYSTEM(DebugSys, debug)
        add(game.ui.get());
        MAKE_SYSTEM(PlayerControlSys, camera)
        
        MAKE_SYSTEM(ChunkManagerSys, chunk_manager)
        MAKE_SYSTEM(ChunkLoaderSys, chunk_loader)
        MAKE_SYSTEM(ChunkGeneratorSys, chunk_generator)
    }
    
    MAKE_SYSTEM(PhysicsSys, physics)
    if(server) {MAKE_SYSTEM(ServerNetworkSys, server_network)}
    if(client) {MAKE_SYSTEM(ClientNetworkSys, client_network)}
    
    if(user) {
        MAKE_SYSTEM(ChunkSys, chunk)
        MAKE_SYSTEM(Terrain, terrain)
        MAKE_SYSTEM(MarchingCubes, marching_cubes)
        add(game.renderer.get());
    }
    
}

