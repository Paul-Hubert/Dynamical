#include "game.h"

#include <iostream>

#include "components/inputc.h"

#include "systems/ui.h"
#include "renderer/renderer.h"

#include "systems/game_set.h"


#include "logic/components/playerc.h"
#include "logic/components/physicsc.h"
#include "logic/components/positionc.h"

#include "util/entt_util.h"

Game::Game(int argc, char** argv) : reg(), settings(reg, argc, argv),
ui(std::make_unique<UISys>(reg)),
renderer(std::make_unique<Renderer>(reg)) {
    
    
    
}

void Game::init() {
    
    game_set = std::make_unique<GameSet>(*this);
    
    game_set->preinit();
    
    auto player = reg.create();
    reg.assign<PlayerC>(player);
    reg.assign<PhysicsC>(player, 60);
    reg.assign<entt::tag<"forces"_hs>>(player);
    reg.assign<PositionC>(player);
    reg.set<Util::Entity<"player"_hs>>(player);
    
    game_set->init();
    
}

void Game::start() {
    
    game_loop.run([this](float dt) {
        
        game_set->tick();
        InputC* input = reg.try_ctx<InputC>();
        if(input != nullptr && input->on[Action::EXIT]) {
            game_loop.setQuitting();
            input->on.set(Action::EXIT, false);
        }
        
    });
    
    game_set->finish();
    
}

Game::~Game() {
    
}
