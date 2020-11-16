#include "main_render.h"

#include "renderer/instance.h"
#include "renderer/device.h"
#include "renderer/swapchain.h"
#include "renderer/camera.h"
#include "renderer/marching_cubes/terrain.h"
#include "util/util.h"
#include "renderer/num_frames.h"
#include "renderer/marching_cubes/chunk.h"
#include "logic/components/chunk/chunkc.h"
#include "logic/components/renderinfo.h"

std::mutex Chunk::mutex;

MainRender::MainRender(Instance& instance, Device& device, Transfer& transfer, Swapchain& swap, Camera& camera) : renderpass(device, swap), ubo(device), chunk_render(device, transfer, swap, renderpass, ubo), ui_render(device, swap, transfer, renderpass), instance(instance), device(device), transfer(transfer), swap(swap), camera(camera) {
    
    commandPool = device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, device.g_i));
    
    auto temp = device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, NUM_FRAMES));
    
    for(int i = 0; i < NUM_FRAMES; i++) {
        
        commandBuffers[i] = temp[i];
        
        fences[i] = device->createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
        
    }
    
}


void MainRender::setup() {
    
    renderpass.setup();
    
}

void MainRender::render(entt::registry& reg, uint32_t index, std::vector<vk::Semaphore> waits, std::vector<vk::Semaphore> signals) {
    
    auto& ri = reg.ctx<RenderInfo>();
    
    device->waitForFences(fences[ri.frame_index], VK_TRUE, std::numeric_limits<uint64_t>::max());
    
    device->resetFences(fences[ri.frame_index]);
    
    static float t = 0.f;
    t += 1.f/60.f;
    
    ubo.pointers[ri.frame_index]->viewproj = camera.getViewProjection();
    ubo.pointers[ri.frame_index]->viewpos = glm::vec4(camera.getViewPosition(), t);
    
    vk::CommandBuffer command = commandBuffers[ri.frame_index];
        
    command.begin(vk::CommandBufferBeginInfo({}, nullptr));
    
    auto clearValues = std::vector<vk::ClearValue> {
        vk::ClearValue(vk::ClearColorValue(std::array<float, 4> { 0.2f, 0.2f, 0.2f, 1.0f })),
        vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0))};
    command.beginRenderPass(vk::RenderPassBeginInfo(renderpass, renderpass.framebuffers[index], vk::Rect2D({}, swap.extent), clearValues.size(), clearValues.data()), vk::SubpassContents::eInline);
    
    command.setViewport(0, vk::Viewport(0, (float) swap.extent.height, swap.extent.width, -((float) swap.extent.height), 0, 1));
    
    command.setScissor(0, vk::Rect2D(vk::Offset2D(), swap.extent));
    
    chunk_render.render(reg, command, ubo.descSets[ri.frame_index]);
    
    ui_render.render(command, ri.frame_index);
    
    command.endRenderPass();
    
    command.end();
    

    auto stages = std::vector<vk::PipelineStageFlags> {vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe};
    
    device.g_mutex->lock();
    device.graphics.submit({vk::SubmitInfo(
        Util::removeElement<vk::Semaphore>(waits, nullptr), waits.data(), stages.data(),
        1, &commandBuffers[ri.frame_index],
        Util::removeElement<vk::Semaphore>(signals, nullptr), signals.data()
    )}, fences[ri.frame_index]);
    device.g_mutex->unlock();
    
}

void MainRender::cleanup() {
    
    device->resetCommandPool(commandPool, {});
    renderpass.cleanup();
    
}


MainRender::~MainRender() {
    
    for(int i = 0; i < commandBuffers.size(); i++) {
        
        device->destroy(fences[i]);
        
    }
    
    device->destroy(commandPool);
    
}
