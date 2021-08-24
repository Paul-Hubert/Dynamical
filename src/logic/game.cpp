#include "game.h"

#include <iostream>
#include <taskflow/taskflow.hpp>

#include "renderer/renderer.h"
#include "systems/system_set.h"

#include "systems/system_list.h"

#include "factories/factory_list.h"

#include "util/entt_util.h"

#include "settings.h"

#include "logic/components/inputc.h"

#include "logic/systems/uisys.h"

#include "extra/optick/optick.h"

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

    set->pre_add<UISys>();

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
