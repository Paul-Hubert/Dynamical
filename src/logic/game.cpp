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
#include "logic/components/vrinputc.h"

#include "renderer/model/model_manager.h"

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

    physics = std::make_unique<PhysicsSys>(reg);

    set = std::make_unique<SystemSet>(reg);

    set->pre_add<VRPlayerControlSys>();

    set->pre_add<EditorControlSys>();

    set->post_add<SpectatorControlSys>();


    // preinit

    tf::Executor& executor = reg.set<tf::Executor>();

    ModelManager& manager = reg.set<ModelManager>(reg);

    renderer->preinit();

    physics->preinit();

    set->preinit();


    // init

    renderer->init();

    physics->init();

    // create basic model
    {

        manager.load("./resources/gltf/box.glb");

        auto box_model = manager.get("./resources/gltf/box.glb");

        entt::entity box = reg.create();
        auto& renderable = reg.emplace<ModelRenderableC>(box, box_model);
        auto& transform = reg.emplace<TransformC>(box);
        transform.transform->setIdentity();
        transform.transform->setOrigin(btVector3(0., 2., 0.));

        ObjectC& box_object = reg.emplace<ObjectC>(box);

        auto& model = reg.get<ModelC>(box_model);
        btRigidBody::btRigidBodyConstructionInfo info(model.mass, static_cast<btMotionState*>(transform.transform.get()), model.shape.get(), model.local_inertia);
        info.m_friction = 0.5;
        box_object.rigid_body = std::make_unique<btRigidBody>(info);

        reg.ctx<btDiscreteDynamicsWorld*>()->addRigidBody(box_object.rigid_body.get());

    }

    set->init();


    InputC& input = reg.ctx<InputC>();
    VRInputC& vr_input = reg.ctx<VRInputC>();

    // start loop
    
    bool running = true;
    while(running) {

        OPTICK_FRAME("Main");

        renderer->prepare();

        float dt = (float) ((vr_input.predicted_period) / 1000000000.);

        if (input.on[Action::EXIT]) {
            running = false;
            input.on.set(Action::EXIT, false);
        }

        set->pre_tick(dt);

        physics->tick(dt);

        set->post_tick(dt);

        renderer->render();
        
    }

}

Game::~Game() {
    
    set->finish();
    physics->finish();
    renderer->finish();

}
