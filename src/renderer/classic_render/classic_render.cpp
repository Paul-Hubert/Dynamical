#include "classic_render.h"

#include "renderer/tree_shade.h"
#include "renderer/lighting/lights.h"

#include "util/log.h"

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"

#include "logic/components/camera.h"
#include "scene/run_args.h"
#include "view/components/input.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

using namespace dy;

ClassicRender::ClassicRender(Context& ctx, entt::registry& reg) : reg(reg), ctx(ctx), renderpass(ctx),
per_frame(NUM_FRAMES) {

    commandPool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, ctx.device.g_i));

    auto cmds = ctx.device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, (uint32_t)per_frame.size()));

    auto sizes = ctx.viewPoolSizes((uint32_t) per_frame.size());
    descriptorPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlags{}, (uint32_t) per_frame.size(), sizes));

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
        ctx.writeViewSet(f.set, (uint32_t) i, true);

        f.acquireSemaphore = ctx.device->createSemaphore(vk::SemaphoreCreateInfo());

    }

    presentSemaphores.resize(ctx.swap.images.size());
    for(auto& semaphore : presentSemaphores) {
        semaphore = ctx.device->createSemaphore(vk::SemaphoreCreateInfo());
    }

    auto* args = reg.ctx().find<RunArgs>();
    if(args && args->gpu_times) {
        uint32_t bits = ctx.device.physical.getQueueFamilyProperties()[ctx.device.g_i].timestampValidBits;
        if(bits == 0) {
            dy::log(dy::warning) << "gpu_times : the graphics queue has no timestamps\n";
        } else {
            times.mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
            times.period = ctx.device.physical.getProperties().limits.timestampPeriod;
            times.pool = ctx.device->createQueryPool(vk::QueryPoolCreateInfo({}, vk::QueryType::eTimestamp, (uint32_t) per_frame.size() * (max_timed_passes + 1)));
            times.written.assign(per_frame.size(), 0);
        }
    }

}

void ClassicRender::readPassTimes(uint32_t frame_index) {

    uint32_t count = times.written[frame_index];
    if(count == 0) return;
    times.written[frame_index] = 0;

    uint64_t ticks[max_timed_passes + 1];
    auto result = ctx.device->getQueryPoolResults(times.pool, frame_index * (max_timed_passes + 1), count + 1,
        (count + 1) * sizeof(uint64_t), ticks, sizeof(uint64_t), vk::QueryResultFlagBits::e64);
    if(result != vk::Result::eSuccess) return;

    times.sums.resize(times.names.size(), 0.);
    for(uint32_t i = 0; i < count; i++) {
        times.sums[i] += (double) ((ticks[i + 1] - ticks[i]) & times.mask) * times.period * 1e-6;
    }
    if(++times.frames >= 300) logPassTimes();

}

void ClassicRender::logPassTimes() {

    if(times.frames == 0) return;

    std::ostringstream line;
    line << std::fixed << std::setprecision(3);
    double total = 0.;
    for(size_t i = 0; i < times.sums.size(); i++) {
        line << times.names[i] << " " << times.sums[i] / times.frames << ", ";
        total += times.sums[i];
    }
    dy::log(dy::info) << "GPU ms over " << times.frames << " frames (" << ctx.swap.extent.width << " x " << ctx.swap.extent.height << ") : "
        << line.str() << "all " << total / times.frames << "\n";

    std::fill(times.sums.begin(), times.sums.end(), 0.);
    times.frames = 0;

}

bool ClassicRender::prepare(uint32_t frame_index) {

    OPTICK_EVENT();

    // The fence of this frame was waited for : what it timed last time is there
    if(times.pool) readPassTimes(frame_index);

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
    ubo->tree_shade = treeShadeView(reg);
    ubo->inverse_view_projection = glm::inverse(ubo->view_projection);
    ubo->previous_view_projection = have_previous ? previous_view_projection : ubo->view_projection;
    ubo->lighting = lightingView(reg, RenderTarget::Desktop);
    previous_view_projection = ubo->view_projection;
    have_previous = true;

    ctx.writeViewSet(per_frame[frame_index].set, frame_index, false);

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

    const uint32_t first_query = frame_index * (max_timed_passes + 1);
    uint32_t timed = 0;
    if(times.pool) command.resetQueryPool(times.pool, first_query, max_timed_passes + 1);

    command.beginRenderPass(vk::RenderPassBeginInfo(renderpass, renderpass.frames[ctx.swap.current].framebuffer, vk::Rect2D({}, extent), clearValues), vk::SubpassContents::eInline);

    command.setViewport(0, vk::Viewport(0.f, 0.f, (float)extent.width, (float)extent.height, 0.f, 1.f));
    command.setScissor(0, vk::Rect2D(vk::Offset2D(), extent));

    RenderView view { RenderTarget::Desktop, frame_index, 0, f.set, extent };

    if(times.pool) command.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, times.pool, first_query);

    for(ScenePass* pass : passes) {
        if(pass->supports(RenderTarget::Desktop)) {
            pass->record(command, view);
            if(times.pool && timed < max_timed_passes) {
                command.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, times.pool, first_query + ++timed);
                if(times.names.size() < timed) times.names.push_back(pass->name());
            }
        }
    }
    if(times.pool) times.written[frame_index] = timed;

    command.endRenderPass();

    if(capture_requested) recordCapture(command);

    command.end();

}

bool ClassicRender::requestCapture() {

    if(!ctx.swap.readable || !acquired) return false;

    // The camera of a shot is held since the shot before : these are the times of its view
    if(times.pool) logPassTimes();

    vk::DeviceSize size = (vk::DeviceSize) ctx.swap.extent.width * ctx.swap.extent.height * 4;
    if(!capture_buffer || capture_buffer.size < size) {
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        capture_buffer = VmaBuffer(ctx.device, &allocInfo, vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));
        capture_buffer.size = size;
    }

    capture_requested = true;
    return true;

}

void ClassicRender::recordCapture(vk::CommandBuffer command) {

    vk::Image image = ctx.swap.images[ctx.swap.current];
    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    vk::Extent2D extent = ctx.swap.extent;

    command.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
        vk::ImageMemoryBarrier(vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead,
            vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferSrcOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, range));

    command.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, capture_buffer,
        vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(), vk::Extent3D(extent.width, extent.height, 1)));

    command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost | vk::PipelineStageFlagBits::eBottomOfPipe, {}, {},
        vk::BufferMemoryBarrier(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, capture_buffer, 0, VK_WHOLE_SIZE),
        vk::ImageMemoryBarrier(vk::AccessFlagBits::eTransferRead, {}, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::ePresentSrcKHR,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, range));

}

void ClassicRender::readCapture(std::vector<uint8_t>& rgb, uint32_t& width, uint32_t& height) {

    capture_requested = false;

    width = ctx.swap.extent.width;
    height = ctx.swap.extent.height;

    vmaInvalidateAllocation(ctx.device, capture_buffer.allocation, 0, VK_WHOLE_SIZE);
    VmaAllocationInfo info;
    vmaGetAllocationInfo(ctx.device, capture_buffer.allocation, &info);
    const uint8_t* pixels = static_cast<const uint8_t*>(info.pMappedData);

    bool bgra = ctx.swap.format == vk::Format::eB8G8R8A8Srgb || ctx.swap.format == vk::Format::eB8G8R8A8Unorm;
    rgb.resize((size_t) width * height * 3);
    for(size_t i = 0; i < (size_t) width * height; i++) {
        rgb[i * 3 + 0] = pixels[i * 4 + (bgra ? 2 : 0)];
        rgb[i * 3 + 1] = pixels[i * 4 + 1];
        rgb[i * 3 + 2] = pixels[i * 4 + (bgra ? 0 : 2)];
    }

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

    if(times.pool) ctx.device->destroy(times.pool);
    ctx.device->destroy(commandPool);
    ctx.device->destroy(descriptorPool);

}
