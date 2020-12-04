#include "vr_render.h"

#define GLM_FORCE_CTOR_INIT

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <thread>

#include "util/log.h"

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"

#include "renderer/util/vk_util.h"

#include "logic/components/inputc.h"

#include "logic/components/vrinputc.h"

VRRender::VRRender(entt::registry& reg, Context& ctx) : reg(reg), ctx(ctx), renderpass(ctx), ubo(ctx), material_manager(reg, ctx),
object_render(reg, ctx, renderpass, std::vector<vk::DescriptorSetLayout>{ubo.descLayout}),
ui_render(ctx, renderpass),
swapchain_image_indices(ctx.vr.swapchains.size()),
per_frame(ctx.vr.swapchains[0].images.size()) {
    
    commandPool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, ctx.device.g_i));

    auto cmds = ctx.device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, (uint32_t) per_frame.size()));

    for(int i = 0; i < per_frame.size(); i++) {
        per_frame[i].commandBuffer = cmds[i];
        per_frame[i].fence = ctx.device->createFence({});
    }

}

static glm::mat4 projection_fov(const XrFovf fov, const float near_z, const float far_z) {

    const float tan_left = tanf(fov.angleLeft);
    const float tan_right = tanf(fov.angleRight);

    const float tan_down = tanf(fov.angleDown);
    const float tan_up = tanf(fov.angleUp);

    const float tan_width = tan_right - tan_left;
    const float tan_height = tan_up - tan_down;

    const float a11 = 2 / tan_width;
    const float a22 = 2 / tan_height;

    const float a31 = (tan_right + tan_left) / tan_width;
    const float a32 = (tan_up + tan_down) / tan_height;
    const float a33 = -far_z / (far_z - near_z);

    const float a43 = -(far_z * near_z) / (far_z - near_z);

    const float mat[16] = {
            a11, 0, 0, 0, 0, a22, 0, 0, a31, a32, a33, -1, 0, 0, a43, 0,
    };

    return glm::make_mat4(mat);
}

static glm::mat4 view_pose(XrPosef& pose) {

    glm::quat quat = glm::quat(pose.orientation.w * -1.0f, pose.orientation.x,
                               pose.orientation.y * -1.0f, pose.orientation.z);
    glm::mat4 rotation = glm::mat4_cast(quat);

    glm::vec3 position =
            glm::vec3(pose.position.x, -pose.position.y, pose.position.z);

    glm::mat4 translation = glm::translate(glm::mat4(1.f), position);
    glm::mat4 view_glm = translation * rotation;

    glm::mat4 view_glm_inv = glm::inverse(view_glm);

    return view_glm_inv;
}



void VRRender::record(vk::CommandBuffer command) {

    OPTICK_EVENT();

    command.begin(vk::CommandBufferBeginInfo({}, nullptr));
    OPTICK_GPU_CONTEXT(command);
    OPTICK_GPU_EVENT("draw");

    for (uint32_t v = 0; v < ctx.vr.swapchains.size(); v++) {
        auto& swapchain = ctx.vr.swapchains[v];
        uint32_t image_index = swapchain_image_indices[v];

        auto clearValues = std::vector<vk::ClearValue>{
                vk::ClearValue(vk::ClearColorValue(std::array<float, 4> { 0.2f, 0.2f, 0.2f, 1.0f })),
                vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0)) };

        command.beginRenderPass(vk::RenderPassBeginInfo(renderpass, renderpass.views[v].framebuffers[image_index], vk::Rect2D({}, vk::Extent2D(swapchain.width, swapchain.height)), (uint32_t)clearValues.size(), clearValues.data()), vk::SubpassContents::eInline);

        command.setViewport(0, vk::Viewport(0.f, 0.f, (float)swapchain.width, (float)swapchain.height, 0.f, 1.f));

        command.setScissor(0, vk::Rect2D(vk::Offset2D(), vk::Extent2D(swapchain.width, swapchain.height)));

        command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object_render.layout, 0, {ubo.views[v].per_frame[image_index].set}, nullptr);

        {

            OPTICK_GPU_EVENT("draw_objects");
            object_render.render(command, frame_index);

        }

        {

            OPTICK_GPU_EVENT("draw_ui");
            ui_render.render(command, frame_index);

        }

        command.endRenderPass();

    }

    command.end();

}

void VRRender::prepare() {

    OPTICK_EVENT();

    VRInputC& vr_input = reg.ctx<VRInputC>();

    if (!vr_input.rendering) {
        return;
    }
    
    material_manager.update();

    frame_index = (frame_index + 1) % per_frame.size();


    // Record with swapchain images

    for (uint32_t v = 0; v < ctx.vr.swapchains.size(); v++) {

        OPTICK_EVENT("acquire_swapchain");
        xrCheckResult(xrAcquireSwapchainImage(ctx.vr.swapchains[v].handle, nullptr, &swapchain_image_indices[v]));

    }

    for (uint32_t v = 0; v < ctx.vr.swapchains.size(); v++) {

        XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wait_info.timeout = XR_INFINITE_DURATION;

        OPTICK_EVENT("wait_swapchain");
        xrCheckResult(xrWaitSwapchainImage(ctx.vr.swapchains[v].handle, &wait_info));

    }

    {

        OPTICK_EVENT("wait_fence");
        ctx.device->waitForFences({ per_frame[frame_index].fence }, VK_TRUE, std::numeric_limits<uint64_t>::max());
        ctx.device->resetFences({ per_frame[frame_index].fence });

    }

    record(per_frame[frame_index].commandBuffer);




    // Begin Frame

    XrFrameState frame_state = { XR_TYPE_FRAME_STATE };
    {

        OPTICK_EVENT("wait_frame");
        xrCheckResult(xrWaitFrame(ctx.vr.session, nullptr, &frame_state));

    }
    vr_input.predicted_time = frame_state.predictedDisplayTime;
    vr_input.predicted_period = frame_state.predictedDisplayPeriod;

    XrResult result;
    {

        OPTICK_EVENT("begin_frame");
        result = xrBeginFrame(ctx.vr.session, nullptr);

    }

    if (result == XR_FRAME_DISCARDED || result == XR_SESSION_NOT_FOCUSED || frame_state.shouldRender == XR_FALSE) {

        dy::log() << "frame discarded\n";
        XrFrameEndInfo end_info{ XR_TYPE_FRAME_END_INFO };
        end_info.displayTime = vr_input.predicted_time;
        end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

        OPTICK_EVENT("end_frame_premature");
        xrCheckResult(xrEndFrame(ctx.vr.session, &end_info));
        return;

    }
    else {

        xrCheckResult(result);

    }

    uint32_t view_count = ctx.vr.num_views;
    XrViewState view_state = { XR_TYPE_VIEW_STATE };
    XrViewLocateInfo locate_info = { XR_TYPE_VIEW_LOCATE_INFO };
    locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate_info.displayTime = vr_input.predicted_time;
    locate_info.space = ctx.vr.space;
    std::vector<XrView> views(view_count, { XR_TYPE_VIEW });
    {

        OPTICK_EVENT("locate_views");
        xrCheckResult(xrLocateViews(ctx.vr.session, &locate_info, &view_state, (uint32_t)views.size(), &view_count, views.data()));

    }

    for(uint32_t v = 0; v < ctx.vr.num_views; v++) {
        vr_input.views[v].pose = views[v].pose;
    }
    
}

void VRRender::render(std::vector<vk::Semaphore> waits, std::vector<vk::Semaphore> signals) {

    OPTICK_EVENT();

    VRInputC& vr_input = reg.ctx<VRInputC>();

    if(!vr_input.rendering) {
        return;
    }


    {

        InputC& input = reg.ctx<InputC>();

        OPTICK_EVENT("sleep");
        if(input.on[Action::LAG]) std::this_thread::sleep_for(std::chrono::nanoseconds(7000000));

    }

    // Get VR view matrices

    uint32_t view_count = ctx.vr.num_views;
    XrViewState view_state = {XR_TYPE_VIEW_STATE};
    XrViewLocateInfo locate_info = {XR_TYPE_VIEW_LOCATE_INFO};
    locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate_info.displayTime = vr_input.predicted_time;
    locate_info.space = ctx.vr.space;
    std::vector<XrView> views(view_count, {XR_TYPE_VIEW});
    {

        OPTICK_EVENT("locate_views");
        xrCheckResult(xrLocateViews(ctx.vr.session, &locate_info, &view_state, (uint32_t)views.size(), &view_count, views.data()));

    }
    
     
    // Update matrices at the very end

    for (uint32_t v = 0; v < ctx.vr.num_views; v++) {
        auto& view = views[v];
        vr_input.views[v].pose = view.pose;

        OPTICK_EVENT("calculate_matrices");
        ubo.views[v].per_frame[swapchain_image_indices[v]].pointer->view_projection = projection_fov(views[v].fov, 0.01f, 100.f) * view_pose(view.pose);
        ubo.views[v].per_frame[swapchain_image_indices[v]].pointer->position = glm::vec4(view.pose.position.x, -view.pose.position.y, view.pose.position.z, 1.0f);

    }


    // Submit command buffer

    {

        auto stages = std::vector<vk::PipelineStageFlags>{ vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe };

        OPTICK_EVENT("submit");
        ctx.device.graphics.submit({ vk::SubmitInfo(
                0, nullptr, stages.data(),
                1, &per_frame[frame_index].commandBuffer,
                0, nullptr
        ) }, per_frame[frame_index].fence);

    }


    for (uint32_t v = 0; v < ctx.vr.num_views; v++) {
        OPTICK_EVENT("release_swapchain");
        xrCheckResult(xrReleaseSwapchainImage(ctx.vr.swapchains[v].handle, nullptr));
    }


    // Present


    std::vector<XrCompositionLayerProjectionView> proj_views(ctx.vr.swapchains.size());

    for(uint32_t v = 0; v < ctx.vr.num_views; v++) {
        auto& view = views[v];

        proj_views[v] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        proj_views[v].pose = view.pose;
        proj_views[v].fov = view.fov;
        proj_views[v].subImage.swapchain = ctx.vr.swapchains[v].handle;
        proj_views[v].subImage.imageRect.offset = {0, 0};
        proj_views[v].subImage.imageRect.extent = {(int32_t)ctx.vr.swapchains[v].width, (int32_t)ctx.vr.swapchains[v].height};

    }

    XrCompositionLayerProjection layer_proj = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer_proj.space = ctx.vr.space;
    layer_proj.viewCount = (uint32_t)proj_views.size();
    layer_proj.views = proj_views.data();

    XrCompositionLayerBaseHeader* layer = reinterpret_cast<XrCompositionLayerBaseHeader*> (&layer_proj);

    XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
    end_info.displayTime = vr_input.predicted_time;
    end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end_info.layerCount = 1;
    end_info.layers = &layer;

    {

        OPTICK_EVENT("end_frame");
        xrCheckResult(xrEndFrame(ctx.vr.session, &end_info));

    }

}



VRRender::~VRRender() {

    for (int i = 0; i < per_frame.size(); i++) {
        ctx.device->destroy(per_frame[i].fence);
    }
    
    ctx.device->destroy(commandPool);
    
}
