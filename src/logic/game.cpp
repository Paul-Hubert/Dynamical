#include "game.h"

#include <iostream>

#include "components/inputc.h"

#include "game_set.h"

#include <taskflow/taskflow.hpp>

#include "logic/components/playerc.h"
#include "factories/factory_list.h"

#include "util/entt_util.h"

#include "logic/components/model_renderablec.h"
#include "logic/components/transformc.h"
#include "logic/components/objectc.h"
#include "renderer/model/model_manager.h"

#include "optick.h"

Game::Game(int argc, char** argv) : reg(), settings(reg, argc, argv), renderer(reg) {
    
}

void Game::init() {

    reg.set<tf::Executor>();
    
    set = std::make_unique<GameSet>(*this);
    
    set->preinit();

    renderer.preinit();
    
    auto player = reg.create();
    reg.emplace<PlayerC>(player);
    reg.set<Util::Entity<"player"_hs>>(player);
    
    set->init();

    renderer.init();

    ModelManager& manager = reg.set<ModelManager>(reg);
    
    {

        manager.load("./resources/box.glb");
        auto box_model = manager.get("./resources/box.glb");

        entt::entity box = reg.create();
        auto& renderable = reg.emplace<ModelRenderableC>(box, box_model);
        auto& transform = reg.emplace<TransformC>(box);
        transform.transform.setIdentity();

        ObjectC& box_object = reg.emplace<ObjectC>(box);
    
        auto& model = reg.get<ModelC>(box_model);
        box_object.shape = model.shape.get();
        box_object.rigid_body = std::make_unique<btRigidBody>(model.mass, static_cast<btMotionState*>(&transform), box_object.shape);

    }
    
}

void Game::start() {
    
    bool running = true;
    while(running) {

        OPTICK_FRAME("MainThread");

        set->tick();

        InputC* input = reg.try_ctx<InputC>();
        if (input != nullptr && input->on[Action::EXIT]) {
            running = false;
            input->on.set(Action::EXIT, false);
        }

        renderer.render();
        
    }

}

Game::~Game() {
    
    set->finish();

    renderer.finish();

}
