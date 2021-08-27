#include "game.h"

#include <iostream>
#include <taskflow/taskflow.hpp>
#include "extra/optick/optick.h"

#include "renderer/renderer.h"
#include "systems/system_set.h"

#include "systems/system_list.h"

#include "factories/factory_list.h"

#include "util/entt_util.h"

#include "settings.h"

#include "logic/components/inputc.h"

#include "logic/systems/input.h"

#include "logic/systems/ui.h"

#include "logic/systems/map_render.h"
#include "logic/systems/ui_render.h"


#include "logic/map/map_manager.h"

Game::Game(int argc, char** argv) {
    registry = std::make_unique<entt::registry>();
    entt::registry& reg = *registry;
    Settings& s = reg.set<Settings>(argc, argv);
}


void Game::start() {

    entt::registry& reg = *registry;

    Settings& s = reg.ctx<Settings>();

    renderer = std::make_unique<Renderer>(reg);

    set = std::make_unique<SystemSet>(reg);

    set->pre_add<InputSys>();
    set->pre_add<CameraSys>();
    
    set->pre_add<UISys>();
    
    
    set->post_add<MapRenderSys>();
    set->post_add<UIRenderSys>();
    
    
    reg.set<MapManager>(reg);
    

    // preinit

    tf::Executor& executor = reg.set<tf::Executor>();

    renderer->preinit();

    set->preinit();


    // init

    renderer->init();

    set->init();


    InputC& input = reg.ctx<InputC>();

    // start loop
    
    bool running = true;
    while(running) {

        OPTICK_FRAME("Main");

        renderer->prepare();

        float dt = (float) (1. / 60.);

        if (input.on[Action::EXIT]) {
            running = false;
            input.on.set(Action::EXIT, false);
        }

        set->pre_tick(dt);

        set->post_tick(dt);

        renderer->render();
        
    }

}

Game::~Game() {
    
    set->finish();
    renderer->finish();

}
