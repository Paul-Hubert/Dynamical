#include "renderer.h"

#include <iostream>
#include <taskflow/taskflow.hpp>

#include "util/util.h"
#include "util/log.h"

#include "context/num_frames.h"

#include "extra/optick/optick.h"
#include "logic/settings/settings.h"

#include "renderer/model/model.h"
#include "renderer/model/model_manager.h"

#include "renderer/passes/terrain_pass.h"
#include "renderer/passes/grass_pass.h"
#include "renderer/passes/model_pass.h"
#include "renderer/passes/vegetation_pass.h"
#include "renderer/passes/animal_pass.h"
#include "renderer/passes/ui_pass.h"

#include "logic/components/frame_clock.h"
#include "logic/components/render_settings.h"
#include "scene/run_args.h"

using namespace dy;

Renderer::Renderer(entt::registry& reg, const RendererDef& desc) : reg(reg),
ctx(reg),
per_frame(NUM_FRAMES) {

    for(auto& f : per_frame) {
        f.fence = ctx.device->createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
        f.transfer_semaphore = ctx.device->createSemaphore({});
        f.compute_semaphore = ctx.device->createSemaphore({});
    }

    reg.ctx().emplace<ModelManager>(reg);
    reg.ctx().emplace<RenderSettings>();

    tree_shade = std::make_unique<TreeShade>(ctx, reg);
    soil_maps = std::make_unique<SoilMaps>(ctx, reg);
    classic = std::make_unique<ClassicRender>(ctx, reg);
    vr = std::make_unique<VRRender>(ctx, reg);

    // Draw order as listed (base: opaque terrain, grass shells on top of it, models, then the desktop UI)
    for(const std::string& name : desc.passes) {
        if(name == "terrain") passes.push_back(std::make_unique<TerrainPass>(ctx, reg));
        else if(name == "grass") passes.push_back(std::make_unique<GrassPass>(ctx, reg));
        else if(name == "vegetation") passes.push_back(std::make_unique<VegetationPass>(ctx, reg));
        else if(name == "animals") passes.push_back(std::make_unique<AnimalPass>(ctx, reg));
        else if(name == "model") passes.push_back(std::make_unique<ModelPass>(ctx, reg));
        else if(name == "ui") passes.push_back(std::make_unique<UIPass>(ctx, reg));
        else dy::log(dy::error) << "Renderer : unknown pass " << name << "\n";
    }

    for(auto& pass : passes) {
        pass_list.push_back(pass.get());
        if(pass->supports(RenderTarget::Desktop)) {
            pass->createPipeline(RenderTarget::Desktop, classic->getRenderpass());
        }
        if(vr->enabled() && pass->supports(RenderTarget::VR)) {
            pass->createPipeline(RenderTarget::VR, vr->getRenderpass());
        }
    }

    start_time = std::chrono::steady_clock::now();

}

void Renderer::preinit() {

}

void Renderer::init() {

    // Startup uploads (textures, fonts, models loaded during init) complete before the first frame
    ctx.transfer.flush({}, {}, {});
    ctx.device->waitIdle();
    ctx.transfer.check();

}

void Renderer::prepare() {

    OPTICK_EVENT();

    ctx.frame_index = (ctx.frame_index + 1) % NUM_FRAMES;

    auto& f = per_frame[ctx.frame_index];

    {
        OPTICK_EVENT("wait_fence");
        ctx.device->waitForFences({ f.fence }, VK_TRUE, std::numeric_limits<uint64_t>::max());
    }

    classic->prepare(ctx.frame_index);

    vr->prepare(ctx.frame_index);

}

void Renderer::render() {

    OPTICK_EVENT();

    auto& f = per_frame[ctx.frame_index];
    uint32_t frame_index = ctx.frame_index;
    float time = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_time).count();
    if(auto* args = reg.ctx().find<RunArgs>(); args && args->deterministic()) {
        time = (float) reg.ctx().get<FrameClock>().time;
    }

    ctx.transfer.check();

    tree_shade->update();
    soil_maps->update();
    for(ScenePass* pass : pass_list) {
        pass->update(frame_index);
    }


    // Transfer -> compute -> graphics, each stage waiting on the previous one if it had work

    std::vector<vk::Semaphore> waits;
    std::vector<vk::PipelineStageFlags> stages;

    bool has_compute = !ctx.compute.isEmpty();

    if(!ctx.transfer.isEmpty()) {
        ctx.transfer.flush({}, {}, {f.transfer_semaphore});
        waits = {f.transfer_semaphore};
        stages = {vk::PipelineStageFlagBits::eAllCommands};
    }

    if(has_compute) {
        ctx.compute.flush(waits, stages, {f.compute_semaphore});
        waits = {f.compute_semaphore};
        stages = {vk::PipelineStageFlagBits::eAllCommands};
    }


    // Record both targets concurrently, they only share read-only scene state

    bool do_classic = classic->active();
    bool do_vr = vr->active();

    if(do_classic) classic->updateView(frame_index, time);
    if(do_vr) {
        vr->acquire();
        vr->updateView(frame_index, time);
    }

    {
        OPTICK_EVENT("record");
        tf::Executor& executor = reg.ctx().get<tf::Executor>();
        tf::Taskflow taskflow;
        if(do_classic) taskflow.emplace([&]() { classic->record(frame_index, pass_list); });
        if(do_vr) taskflow.emplace([&]() { vr->record(frame_index, pass_list); });
        executor.run(taskflow).wait();
    }


    // One graphics submission per frame for both targets, it also consumes the transfer/compute semaphores

    std::vector<vk::CommandBuffer> commands;
    std::vector<vk::Semaphore> signals;

    if(do_classic) {
        commands.push_back(classic->getCommandBuffer(frame_index));
        waits.push_back(classic->getAcquireSemaphore(frame_index));
        stages.push_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        signals.push_back(classic->getPresentSemaphore());
    }

    if(do_vr) {
        commands.push_back(vr->getCommandBuffer(frame_index));
    }

    ctx.device->resetFences({ f.fence });

    {
        OPTICK_EVENT("submit");
        ctx.device.graphics.submit({ vk::SubmitInfo(waits, stages, commands, signals) }, f.fence);
    }

    vr->endFrame();

    classic->present();

}

bool Renderer::requestCapture() {
    return classic->requestCapture();
}

bool Renderer::takeCapture(std::vector<uint8_t>& rgb, uint32_t& width, uint32_t& height) {

    auto& f = per_frame[ctx.frame_index];
    ctx.device->waitForFences({ f.fence }, VK_TRUE, std::numeric_limits<uint64_t>::max());
    classic->readCapture(rgb, width, height);
    return !rgb.empty();

}

void Renderer::finish() {

    ctx.device->waitIdle();

    // GPU resources stored in the registry must go before the device
    reg.clear<ModelC>();
    reg.ctx().erase<ModelManager>();

}

Renderer::~Renderer() {

    passes.clear();
    pass_list.clear();
    vr.reset();
    classic.reset();
    soil_maps.reset();
    tree_shade.reset();

    for(auto& f : per_frame) {
        ctx.device->destroy(f.fence);
        ctx.device->destroy(f.transfer_semaphore);
        ctx.device->destroy(f.compute_semaphore);
    }

}
