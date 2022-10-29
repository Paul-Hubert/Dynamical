#include "classic_render.h"

#include "util/log.h"

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"

#include "renderer/util/vk_util.h"

#include "logic/components/camera.h"

using namespace dy;

ClassicRender::ClassicRender(Context& ctx, entt::registry& reg) : reg(reg), ctx(ctx), renderpass(ctx),
per_frame(NUM_FRAMES) {
    
    auto bindings = std::vector{
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1,
                                               vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
    };
    view_layout = ctx.device->createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo({}, (uint32_t)bindings.size(), bindings.data()));
    
    
    commandPool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo({}, ctx.device.g_i));

    auto cmds = ctx.device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, (uint32_t)per_frame.size()));

    vk::DescriptorPoolSize size(vk::DescriptorType::eUniformBuffer, (uint32_t) per_frame.size());
    descriptorPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlags{}, (uint32_t) per_frame.size(), 1, &size));

    for (int i = 0; i < per_frame.size(); i++) {
        auto& f = per_frame[i];
        f.commandBuffer = cmds[i];
        f.fence = ctx.device->createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));

        f.set = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descriptorPool, 1, &view_layout))[0];

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

        f.acquireSemaphore = ctx.device->createSemaphore(vk::SemaphoreCreateInfo(vk::SemaphoreCreateFlags{}));
        f.presentSemaphore = ctx.device->createSemaphore(vk::SemaphoreCreateInfo(vk::SemaphoreCreateFlags{}));

    }

}




void ClassicRender::prepare() {

    OPTICK_EVENT();
    
    auto& f = per_frame[ctx.frame_index];

    {

        OPTICK_EVENT("wait_fence");
        ctx.device->waitForFences({ f.fence }, VK_TRUE, std::numeric_limits<uint64_t>::max());
        ctx.device->resetFences({ f.fence });

    }


    ctx.swap.current = ctx.swap.acquire(f.acquireSemaphore);

    command = f.commandBuffer;
    
    // Record with swapchain images

    ctx.device->resetCommandPool(commandPool);

    command.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr));
    OPTICK_GPU_CONTEXT(command);
    OPTICK_GPU_EVENT("draw");

    auto clearValues = std::vector<vk::ClearValue>{
            vk::ClearValue(vk::ClearColorValue(std::array<float, 4> { 0.2f, 0.2f, 0.2f, 1.0f })),
            vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0)) };

    command.beginRenderPass(vk::RenderPassBeginInfo(renderpass, renderpass.frames[ctx.swap.current].framebuffer, vk::Rect2D({}, vk::Extent2D(ctx.swap.extent.width, ctx.swap.extent.height)), (uint32_t)clearValues.size(), clearValues.data()), vk::SubpassContents::eInline);

    command.setViewport(0, vk::Viewport(0.f, 0.f, (float)ctx.swap.extent.width, (float)ctx.swap.extent.height, 0.f, 1.f));

    command.setScissor(0, vk::Rect2D(vk::Offset2D(), vk::Extent2D(ctx.swap.extent.width, ctx.swap.extent.height)));

}

void ClassicRender::render(vk::Semaphore semaphore) {

    OPTICK_EVENT();
    
    auto& f = per_frame[ctx.frame_index];

    auto command = f.commandBuffer;
    
    command.endRenderPass();

    command.end();
    

    {
        auto& camera = reg.ctx<Camera>();

        f.pointer->projection = camera.getProjection();
        f.pointer->view = camera.getView();

        f.pointer->screen_size = camera.getScreenSize();

    }

    // Submit command buffer

    {

        
        
        std::vector<vk::Semaphore> semaphores;
        auto stages = std::vector<vk::PipelineStageFlags>();
        
        semaphores.push_back(f.acquireSemaphore);
        stages.push_back(vk::PipelineStageFlagBits::eTopOfPipe);
        
        if(semaphore) {
            semaphores.push_back(semaphore);
            stages.push_back(vk::PipelineStageFlagBits::eVertexInput);
        }
        
        OPTICK_EVENT("submit");
        ctx.device.graphics.submit({ vk::SubmitInfo(
                semaphores.size(), semaphores.data(), stages.data(),
                1, &command,
                1, &f.presentSemaphore
        ) }, f.fence);

        ctx.device->waitForFences({ f.fence }, VK_TRUE, std::numeric_limits<uint64_t>::max());

    }

    ctx.swap.present(f.presentSemaphore);

}



ClassicRender::~ClassicRender() {

    for (int i = 0; i < per_frame.size(); i++) {
        ctx.device->destroy(per_frame[i].fence);
        ctx.device->destroy(per_frame[i].acquireSemaphore);
        ctx.device->destroy(per_frame[i].presentSemaphore);
    }

    ctx.device->destroy(commandPool);
    ctx.device->destroy(descriptorPool);
    ctx.device->destroy(view_layout);

}
