#include "game.h"

#include <iostream>

#include "components/inputc.h"

#include "systems/ui.h"
#include "renderer/renderer.h"

#include "game_set.h"


#include "logic/components/playerc.h"
#include "logic/components/physicsc.h"
#include "logic/components/positionc.h"
#include "logic/components/model/imageuploadc.h"

#include "util/entt_util.h"

Game::Game(int argc, char** argv) : reg(), settings(reg, argc, argv),
ui(std::make_unique<UISys>(reg)),
renderer(std::make_unique<Renderer>(reg)) {
    
    
    
}

void Game::init() {
    
    set = std::make_unique<GameSet>(*this);
    
    set->preinit();
    
    auto player = reg.create();
    reg.emplace<PlayerC>(player);
    //reg.emplace<PhysicsC>(player, 60);
    reg.emplace<entt::tag<"forces"_hs>>(player);
    reg.emplace<PositionC>(player);
    reg.set<Util::Entity<"player"_hs>>(player);
    
    set->init();

    
}

void Game::start() {
    
    game_loop.run([this](float dt) {
        
        set->tick();
        InputC* input = reg.try_ctx<InputC>();
        if(input != nullptr && input->on[Action::EXIT]) {
            game_loop.setQuitting();
            input->on.set(Action::EXIT, false);
        }
        
    });
    
    set->finish();
    
}

Game::~Game() {
    
}
