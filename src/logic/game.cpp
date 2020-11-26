#include "game.h"

#include <iostream>

#include "components/inputc.h"

#include "systems/ui.h"
#include "renderer/renderer.h"

#include "game_set.h"

#include <taskflow/taskflow.hpp>
#include "util/services/gltf_loader.h"

#include "logic/components/playerc.h"
#include "factories/factory_list.h"

#include "util/entt_util.h"

Game::Game(int argc, char** argv) : reg(), settings(reg, argc, argv),
ui(std::make_unique<UISys>(reg)),
renderer(std::make_unique<Renderer>(reg)) {
    
    
    
}

void Game::init() {

    reg.set<tf::Executor>();
    auto& loader = reg.set<GLTFLoader>(reg);
    
    set = std::make_unique<GameSet>(*this);
    
    set->preinit();
    
    auto player = reg.create();
    reg.emplace<PlayerC>(player);
    reg.set<Util::Entity<"player"_hs>>(player);
    
    set->init();

    entt::entity box_model = loader.load("./resources/Box.glb");

    entt::entity box = ObjectFactory::build(reg, box_model);
    
}

void Game::start() {
    
    bool running = true;
    while(running) {
        
        set->tick();
        InputC* input = reg.try_ctx<InputC>();
        if(input != nullptr && input->on[Action::EXIT]) {
            running = false;
            input->on.set(Action::EXIT, false);
        }
        
    }
    
    
}

Game::~Game() {
    
    set->finish();

}
