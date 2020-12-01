#include "game.h"

#include <iostream>

#include "renderer/renderer.h"
#include "systems/physics.h"
#include "systems/system_set.h"

#include <taskflow/taskflow.hpp>

#include "logic/components/playerc.h"
#include "factories/factory_list.h"

#include "util/entt_util.h"

#include "settings.h"

#include "logic/components/model_renderablec.h"
#include "logic/components/transformc.h"
#include "logic/components/objectc.h"
#include "logic/components/inputc.h"

#include "renderer/model/model_manager.h"

#include "optick.h"

Game::Game(int argc, char** argv) {
    Settings& s = reg.set<Settings>(reg, argc, argv);
}


void Game::start() {

    Settings& s = reg.ctx<Settings>();

    bool server = s.server_side;
    bool client = s.client_side;
    bool multiplayer = server || client;
    bool singleplayer = !multiplayer;
    bool user = client || singleplayer;


    renderer = std::make_unique<Renderer>(reg);

    pre = std::make_unique<SystemSet>(reg);

    physics = std::make_unique<PhysicsSys>(reg);

    post = std::make_unique<SystemSet>(reg);


    // preinit

    pre->preinit();
    physics->preinit();
    post->preinit();
    renderer->preinit();

    tf::Executor& executor = reg.set<tf::Executor>();

    ModelManager& manager = reg.set<ModelManager>(reg);

    auto player = reg.create();
    reg.emplace<PlayerC>(player);
    reg.set<Util::Entity<"player"_hs>>(player);

    // init

    pre->init();
    physics->init();
    post->init();
    renderer->init();


    // create basic model
    {

        manager.load("./resources/box.glb");
        auto box_model = manager.get("./resources/box.glb");

        entt::entity box = reg.create();
        auto& renderable = reg.emplace<ModelRenderableC>(box, box_model);
        auto& transform = reg.emplace<TransformC>(box);
        transform.transform.setIdentity();
        transform.transform.setOrigin(btVector3(0., 1., 0.));

        ObjectC& box_object = reg.emplace<ObjectC>(box);

        auto& model = reg.get<ModelC>(box_model);
        box_object.shape = model.shape.get();
        box_object.rigid_body = std::make_unique<btRigidBody>(model.mass, static_cast<btMotionState*>(&transform), box_object.shape);

        reg.ctx<btDiscreteDynamicsWorld*>()->addRigidBody(box_object.rigid_body.get());

    }

    InputC& input = reg.ctx<InputC>();

    // start loop
    
    bool running = true;
    while(running) {

        OPTICK_FRAME("MainThread");

        renderer->update();

        if (input.on[Action::EXIT]) {
            running = false;
            input.on.set(Action::EXIT, false);
        }

        pre->tick();

        renderer->prepare();

        physics->tick();

        renderer->render();
        
    }

}

Game::~Game() {
    
    post->finish();

    physics->finish();

    pre->finish();

    renderer->finish();

}
