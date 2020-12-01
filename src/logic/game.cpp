#include "game.h"

#include <iostream>
#include <taskflow/taskflow.hpp>

#include "renderer/renderer.h"
#include "systems/physics.h"
#include "systems/system_set.h"

#include "systems/system_list.h"

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
    registry = std::make_unique<entt::registry>();
    entt::registry& reg = *registry;
    Settings& s = reg.set<Settings>(reg, argc, argv);
}


void Game::start() {

    entt::registry& reg = *registry;

    Settings& s = reg.ctx<Settings>();

    bool server = s.server_side;
    bool client = s.client_side;
    bool multiplayer = server || client;
    bool singleplayer = !multiplayer;
    bool user = client || singleplayer;


    renderer = std::make_unique<Renderer>(reg);

    physics = std::make_unique<PhysicsSys>(reg);

    pre_sets = std::make_unique<SystemSet>(reg);
    update_sets = std::make_unique<SystemSet>(reg);
    post_sets = std::make_unique<SystemSet>(reg);

    pre_sets->add<VRPlayerControlSys>();

    // preinit

    renderer->preinit();
    physics->preinit();
    pre_sets->preinit();
    update_sets->preinit();
    post_sets->preinit();

    tf::Executor& executor = reg.set<tf::Executor>();

    ModelManager& manager = reg.set<ModelManager>(reg);

    auto player = reg.create();
    reg.emplace<PlayerC>(player);
    reg.set<Util::Entity<"player"_hs>>(player);


    // init

    renderer->init();
    physics->init();
    pre_sets->init();
    update_sets->init();
    post_sets->init();


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

        pre_sets->tick();

        renderer->prepare();

        update_sets->tick();

        physics->tick();

        post_sets->tick();

        renderer->render();
        
    }

}

Game::~Game() {
    
    post_sets->finish();
    update_sets->finish();
    pre_sets->finish();
    physics->finish();
    renderer->finish();

}
