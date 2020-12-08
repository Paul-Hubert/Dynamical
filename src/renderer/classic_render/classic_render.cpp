#include "classic_render.h"

#include <glm/glm.hpp>
#include "glm/gtc/matrix_transform.hpp"
#include <glm/gtx/quaternion.hpp>

#include "util/log.h"

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"

#include "renderer/util/vk_util.h"

#include "logic/components/inputc.h"
#include "logic/components/vrinputc.h"

ClassicRender::ClassicRender(entt::registry& reg, Context& ctx, vk::DescriptorSetLayout set_layout) : reg(reg), ctx(ctx), renderpass(ctx),
per_frame(NUM_FRAMES) {

    commandPool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, ctx.device.g_i));

    auto cmds = ctx.device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, (uint32_t)per_frame.size()));

    vk::DescriptorPoolSize size(vk::DescriptorType::eUniformBuffer, (uint32_t) per_frame.size());
    descriptorPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlags{}, (uint32_t) per_frame.size(), 1, &size));

    for (int i = 0; i < per_frame.size(); i++) {
        auto& f = per_frame[i];
        f.commandBuffer = cmds[i];
        f.fence = ctx.device->createFence({});

        f.set = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descriptorPool, 1, &set_layout))[0];

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




void ClassicRender::prepare(uint32_t frame_index, std::function<void(vk::CommandBuffer)>& recorder, vk::PipelineLayout pipeline_layout) {

    OPTICK_EVENT();

    {

        OPTICK_EVENT("wait_fence");
        ctx.device->waitForFences({ per_frame[frame_index].fence }, VK_TRUE, std::numeric_limits<uint64_t>::max());
        ctx.device->resetFences({ per_frame[frame_index].fence });

    }

    auto& f = per_frame[frame_index];

    uint32_t image_index = ctx.swap.acquire(f.acquireSemaphore);

    auto command = f.commandBuffer;

    // Record with swapchain images

    command.begin(vk::CommandBufferBeginInfo({}, nullptr));
    OPTICK_GPU_CONTEXT(command);
    OPTICK_GPU_EVENT("draw");

    auto clearValues = std::vector<vk::ClearValue>{
            vk::ClearValue(vk::ClearColorValue(std::array<float, 4> { 0.2f, 0.2f, 0.2f, 1.0f })),
            vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0)) };

    command.beginRenderPass(vk::RenderPassBeginInfo(renderpass, renderpass.frames[image_index].framebuffer, vk::Rect2D({}, vk::Extent2D(ctx.swap.extent.width, ctx.swap.extent.height)), (uint32_t)clearValues.size(), clearValues.data()), vk::SubpassContents::eInline);

    command.setViewport(0, vk::Viewport(0.f, 0.f, (float)ctx.swap.extent.width, (float)ctx.swap.extent.height, 0.f, 1.f));

    command.setScissor(0, vk::Rect2D(vk::Offset2D(), vk::Extent2D(ctx.swap.extent.width, ctx.swap.extent.height)));

    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, { per_frame[frame_index].set }, nullptr);

    recorder(command);

    command.endRenderPass();

    command.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier(
        vk::AccessFlagBits::eColorAttachmentWrite, {}, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, ctx.swap.images[image_index], vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
    ));

    command.end();


}

void ClassicRender::render(uint32_t frame_index) {

    OPTICK_EVENT();

    auto& f = per_frame[frame_index];

    VRInputC& vr_input = reg.ctx<VRInputC>();

    // Update matrices at the very end
    {
        OPTICK_EVENT("calculate_matrices");

        auto& v = vr_input.views[0];

        glm::mat4 projection = glm::perspective(90.f, (float)ctx.swap.extent.width / ctx.swap.extent.height, 0.1f, 100.f);

        glm::quat quat = glm::quat(v.pose.orientation.w * -1.0f, v.pose.orientation.x,
            v.pose.orientation.y * -1.0f, v.pose.orientation.z);
        glm::mat4 rotation = glm::mat4_cast(quat);

        glm::vec3 position =
            glm::vec3(v.pose.position.x, -v.pose.position.y, v.pose.position.z);

        glm::mat4 translation = glm::translate(glm::mat4(1.f), position);
        glm::mat4 view = translation * rotation;

        view = glm::inverse(view);
        per_frame[frame_index].pointer->view_projection = projection * view;
        per_frame[frame_index].pointer->position = glm::vec4(v.pose.position.x, v.pose.position.y, v.pose.position.z, 1.0f);
    }

    // Submit command buffer

    {

        auto stages = std::vector<vk::PipelineStageFlags>{ vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe };

        OPTICK_EVENT("submit");
        ctx.device.graphics.submit({ vk::SubmitInfo(
                1, &per_frame[frame_index].acquireSemaphore, stages.data(),
                1, &per_frame[frame_index].commandBuffer,
                1, &per_frame[frame_index].presentSemaphore
        ) }, per_frame[frame_index].fence);

    }

    ctx.swap.present(per_frame[frame_index].presentSemaphore);

}



ClassicRender::~ClassicRender() {

    for (int i = 0; i < per_frame.size(); i++) {
        ctx.device->destroy(per_frame[i].fence);
    }

    ctx.device->destroy(commandPool);

}
