#include "classic_render.h"

#include "util/log.h"

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"

#include "logic/components/camera.h"
#include "logic/components/input.h"

using namespace dy;

ClassicRender::ClassicRender(Context& ctx, entt::registry& reg) : reg(reg), ctx(ctx), renderpass(ctx),
per_frame(NUM_FRAMES) {

    commandPool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, ctx.device.g_i));

    auto cmds = ctx.device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, (uint32_t)per_frame.size()));

    vk::DescriptorPoolSize size(vk::DescriptorType::eUniformBuffer, (uint32_t) per_frame.size());
    descriptorPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlags{}, (uint32_t) per_frame.size(), 1, &size));

    for (int i = 0; i < per_frame.size(); i++) {
        auto& f = per_frame[i];
        f.commandBuffer = cmds[i];

        f.set = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descriptorPool, 1, &ctx.view_layout))[0];

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        f.ubo = VmaBuffer(ctx.device, &allocInfo, vk::BufferCreateInfo({}, sizeof(ViewUBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));

        VmaAllocationInfo inf;
        vmaGetAllocationInfo(ctx.device, f.ubo.allocation, &inf);
        f.pointer = static_cast<ViewUBO*> (inf.pMappedData);

        auto bufInfo = vk::DescriptorBufferInfo(f.ubo, 0, sizeof(ViewUBO));

        ctx.device->updateDescriptorSets({
            vk::WriteDescriptorSet(f.set, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo, nullptr)
            }, {});

        f.acquireSemaphore = ctx.device->createSemaphore(vk::SemaphoreCreateInfo());

    }

    presentSemaphores.resize(ctx.swap.images.size());
    for(auto& semaphore : presentSemaphores) {
        semaphore = ctx.device->createSemaphore(vk::SemaphoreCreateInfo());
    }

}

bool ClassicRender::prepare(uint32_t frame_index) {

    OPTICK_EVENT();

    acquired = false;

    auto* input = reg.ctx().find<Input>();
    if(input && !input->window_showing) {
        return false;
    }

    ctx.swap.acquire(per_frame[frame_index].acquireSemaphore);
    acquired = true;

    return true;

}

void ClassicRender::updateView(uint32_t frame_index, float time) {

    auto& camera = reg.ctx().get<Camera>();
    float aspect = (float) ctx.swap.extent.width / (float) ctx.swap.extent.height;

    auto* ubo = per_frame[frame_index].pointer;
    ubo->view_projection = camera.getProjection(aspect) * camera.getView();
    ubo->position = glm::vec4(camera.position, time);

}

void ClassicRender::record(uint32_t frame_index, const std::vector<ScenePass*>& passes) {

    OPTICK_EVENT();

    auto& f = per_frame[frame_index];
    auto command = f.commandBuffer;

    command.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr));

    auto clearValues = std::vector<vk::ClearValue>{
            vk::ClearValue(vk::ClearColorValue(std::array<float, 4> { 0.45f, 0.6f, 0.8f, 1.0f })),
            vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0)) };

    vk::Extent2D extent = ctx.swap.extent;

    command.beginRenderPass(vk::RenderPassBeginInfo(renderpass, renderpass.frames[ctx.swap.current].framebuffer, vk::Rect2D({}, extent), clearValues), vk::SubpassContents::eInline);

    command.setViewport(0, vk::Viewport(0.f, 0.f, (float)extent.width, (float)extent.height, 0.f, 1.f));
    command.setScissor(0, vk::Rect2D(vk::Offset2D(), extent));

    RenderView view { RenderTarget::Desktop, frame_index, 0, f.set, extent };

    for(ScenePass* pass : passes) {
        if(pass->supports(RenderTarget::Desktop)) {
            pass->record(command, view);
        }
    }

    command.endRenderPass();

    command.end();

}

vk::Semaphore ClassicRender::getPresentSemaphore() {
    return presentSemaphores[ctx.swap.current];
}

void ClassicRender::present() {

    if(!acquired) return;

    ctx.swap.present(getPresentSemaphore());
    acquired = false;

}

ClassicRender::~ClassicRender() {

    for (auto& f : per_frame) {
        ctx.device->destroy(f.acquireSemaphore);
    }
    for (auto& semaphore : presentSemaphores) {
        ctx.device->destroy(semaphore);
    }

    ctx.device->destroy(commandPool);
    ctx.device->destroy(descriptorPool);

}
