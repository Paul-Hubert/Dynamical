#include "game.h"

#include <iostream>
#include <taskflow/taskflow.hpp>
#include "extra/optick/optick.h"

#include "renderer/renderer.h"
#include "systems/system_set.h"

#include "systems/system_list.h"

#include "factories/factory_list.h"

#include "util/entt_util.h"

#include "logic/settings/settings.h"

#include "logic/components/input.h"

#include "logic/systems/input.h"

#include "logic/systems/ui.h"

#include "logic/systems/selection.h"

#include "logic/systems/map_upload.h"
#include "logic/systems/particle_simulation.h"
#include "logic/systems/map_render.h"
#include "logic/systems/object_render.h"
#include "logic/systems/ui_render.h"
#include "logic/systems/water_flow.h"
#include "logic/systems/conversation.h"

#include "ai/ai.h"
#include "ai/speech/speech_bubble_sys.h"
#include "ai/speech/speech_bubble_render_sys.h"
#include <ai/action_registry.h>
#include <ai/conversation/conversation_manager.h>
#include "llm/llm_manager.h"

#include "util/log.h"

#include "logic/map/map_manager.h"


#include <chrono>

using namespace std::chrono_literals;

using namespace dy;

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
    set->pre_add<UISys>();
    set->pre_add<CameraSys>();
    set->pre_add<TimeSys>();
    set->pre_add<DevMenuSys>();
    set->pre_add<MapConfiguratorSys>();
    set->pre_add<SelectionSys>();
    set->pre_add<MapEditorSys>();
    
    //set->pre_add<ChunkGenerationSys>();
    set->pre_add<WaterFlowSys>();
    
    set->pre_add<PatherSys>();
    set->pre_add<BasicNeedsSys>();
    set->pre_add<HarvestSys>();
    set->pre_add<EatSys>();
    
    set->pre_add<AISys>();
    set->pre_add<SpeechBubbleSys>();
    set->pre_add<LLMDebugSys>();
    set->pre_add<PersonalityInspectorSys>();

    set->pre_add<ActionBarSys>();
    set->pre_add<ConversationSys>();

    set->post_add<MapUploadSys>();
    set->post_add<ParticleSimulationSys>();
    set->post_add<MapRenderSys>();
    set->post_add<ObjectRenderSys>();
    set->post_add<SpeechBubbleRenderSys>();
    set->post_add<UIRenderSys>();


    reg.set<MapManager>(reg);
    reg.set<ConversationManager>(reg);

    // Initialize action registry with all action descriptors
    ActionRegistry::instance().initialize_descriptors();

    // Initialize LLM manager if enabled
    if (s.llm.enabled) {
        llm_manager = std::make_unique<LLMManager>(s.llm.worker_threads);
        llm_manager->configure(s.llm.provider, s.llm.model, s.llm.api_key);
        llm_manager->set_rate_limit(static_cast<int>(s.llm.rate_limit_rps));
        llm_manager->set_cache_enabled(s.llm.caching_enabled);
        reg.set<LLMManager*>(llm_manager.get());
        if (auto* ai_sys = set->get<AISys>()) {
            ai_sys->set_llm_manager(llm_manager.get());
        }
    }

    // preinit

    tf::Executor& executor = reg.set<tf::Executor>();

    renderer->preinit();

    set->preinit();


    // init

    renderer->init();

    set->init();

    Input& input = reg.ctx<Input>();

    // start loop

    using clock = std::chrono::high_resolution_clock;

    auto start = clock::now();

    bool running = true;
    while(running) {

        OPTICK_FRAME("Main");

        renderer->prepare();

        if (input.on[Input::EXIT]) {
            running = false;
            input.on.set(Input::EXIT, false);
        }

        double dt = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - start).count() / 1000000000.0;
        start = clock::now();

        //log(Level::info) << "FPS : " << 1/dt << "\n";

        set->pre_tick(dt);

        set->post_tick(dt);

        renderer->render();
        
    }

}

Game::~Game() {

    if (llm_manager) llm_manager->shutdown();
    set->finish();
    renderer->finish();

}
