#include "game.h"

#include <iostream>

#include "components/inputc.h"

#include "systems/ui.h"
#include "renderer/renderer.h"

#include "game_set.h"


#include "logic/components/playerc.h"
#include "logic/components/physicsc.h"
#include "logic/components/positionc.h"
#include "logic/components/model/bufferuploadc.h"
#include "logic/components/model/meshc.h"
#include "logic/components/model/meshinstancec.h"

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

    Context& ctx = *reg.ctx<Context*>();

    {
        auto mesh = reg.create();
        reg.emplace<entt::tag<"uploading"_hs>>(mesh);
        auto &meshc = reg.emplace<MeshC>(mesh);

        meshc.vertices.push_back({{0, 0, 0},
                                  {},
                                  {}});
        meshc.vertices.push_back({{1, 0, 0},
                                  {},
                                  {}});
        meshc.vertices.push_back({{1, 1, 0},
                                  {},
                                  {}});

        meshc.indices.push_back(0);
        meshc.indices.push_back(1);
        meshc.indices.push_back(2);

        meshc.index_buffer = reg.create();
        vk::BufferCreateInfo info({}, meshc.indices.size() * sizeof(uint16_t), vk::BufferUsageFlagBits::eIndexBuffer,
                                  vk::SharingMode::eExclusive, 1, &ctx.device.g_i);
        auto& index = reg.emplace<BufferUploadC>(meshc.index_buffer, ctx, info);
        memcpy(index.data, meshc.indices.data(), index.size);

        meshc.vertex_buffer = reg.create();
        info.size = meshc.vertices.size() * sizeof(MeshC::Vertex);
        info.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        auto& vertex = reg.emplace<BufferUploadC>(meshc.vertex_buffer, ctx, info);
        memcpy(vertex.data, meshc.vertices.data(), vertex.size);

        auto object = reg.create();
        reg.emplace<MeshInstanceC>(object, mesh);
    }
    
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
