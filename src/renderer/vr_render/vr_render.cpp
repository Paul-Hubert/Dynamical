#include "vr_render.h"

#include "renderer/context.h"
#include "renderer/camera.h"
#include "util/util.h"
#include "logic/components/inputc.h"
#include "renderer/num_frames.h"

#define GLM_FORCE_CTOR_INIT

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>


VRRender::VRRender(entt::registry& reg, Context& ctx) : reg(reg), ctx(ctx), renderpass(ctx), ubo(ctx),  object_render(reg, ctx, renderpass, ubo), ui_render(ctx, renderpass) {
    
    commandPool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, ctx.device.g_i));

    auto cmds = ctx.device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, NUM_FRAMES));

    for(int i = 0; i < NUM_FRAMES; i++) {
        commandBuffers[i] = cmds[i];
    }
    fence = ctx.device->createFence({});

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

    glm::quat quat = glm::quat(pose.orientation.w * -1.0, pose.orientation.x,
                               pose.orientation.y * -1.0, pose.orientation.z);
    glm::mat4 rotation = glm::mat4_cast(quat);

    glm::vec3 position =
            glm::vec3(pose.position.x, -pose.position.y, pose.position.z);

    glm::mat4 translation = glm::translate(glm::mat4(1.f), position);
    glm::mat4 view_glm = translation * rotation;

    glm::mat4 view_glm_inv = glm::inverse(view_glm);

    return view_glm_inv;
}




void VRRender::render(std::vector<vk::Semaphore> waits, std::vector<vk::Semaphore> signals) {

    if(!ctx.vr.rendering) {
        return;
    }


    static float t = 0.f;
    t += 1.f / 60.f;

    //std::vector<VkSemaphore> wait_semaphores;
    //std::vector<VkSemaphore> signal_semaphores;

    // uint32_t swapchain_index = ctx.swap.acquire(waitsems[window_index]);
    // ctx->swap.present(signalsems[ctx->frame_index]);

    
    if(future.valid()) future.wait();

    taskflow.clear();


    // Pre record command buffer

    frame_index = (frame_index + 1) % NUM_FRAMES;

    vk::CommandBuffer command = commandBuffers[frame_index];

    command.begin(vk::CommandBufferBeginInfo({}, nullptr));


    for(uint32_t v = 0; v < ctx.vr.swapchains.size(); v++) {
        auto& swapchain = ctx.vr.swapchains[v];

        uint32_t image_index;
        xrCheckResult(xrAcquireSwapchainImage(swapchain.handle, nullptr, &image_index));

        XrSwapchainImageWaitInfo wait_info = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait_info.timeout = XR_INFINITE_DURATION;
        xrCheckResult(xrWaitSwapchainImage(swapchain.handle, &wait_info));



        auto clearValues = std::vector<vk::ClearValue>{
                vk::ClearValue(vk::ClearColorValue(std::array<float, 4> { 0.2f, 0.2f, 0.2f, 1.0f })),
                vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0))};
        command.beginRenderPass(vk::RenderPassBeginInfo(renderpass, renderpass.views[v].framebuffers[image_index], vk::Rect2D({}, vk::Extent2D(swapchain.width, swapchain.height)), clearValues.size(), clearValues.data()), vk::SubpassContents::eInline);

        command.setViewport(0, vk::Viewport(0, 0, swapchain.width, swapchain.height, 0, 1));

        command.setScissor(0, vk::Rect2D(vk::Offset2D(), vk::Extent2D(swapchain.width, swapchain.height)));

        object_render.render(command, ubo.views[v].descSets[0]);

        ui_render.render(command, 0);

        command.endRenderPass();

    }

    command.end();



    


    // Begin Frame

    XrFrameState frame_state = {XR_TYPE_FRAME_STATE};
    xrCheckResult(xrWaitFrame(ctx.vr.session, nullptr, &frame_state));
    XrTime predicted_time = frame_state.predictedDisplayTime;

    XrResult result = xrBeginFrame(ctx.vr.session, nullptr);

    if(result == XR_FRAME_DISCARDED || result == XR_SESSION_NOT_FOCUSED || frame_state.shouldRender == XR_FALSE) {
        Util::log(Util::trace) << "frame discarded\n";
        XrFrameEndInfo end_info {XR_TYPE_FRAME_END_INFO};
        end_info.displayTime = predicted_time;
        end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        xrCheckResult(xrEndFrame(ctx.vr.session, &end_info));
        return;
    } else xrCheckResult(result);



    // Get VR view matrices

    uint32_t view_count = 0;
    XrViewState view_state = {XR_TYPE_VIEW_STATE};
    XrViewLocateInfo locate_info = {XR_TYPE_VIEW_LOCATE_INFO};
    locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate_info.displayTime = predicted_time;
    locate_info.space = ctx.vr.space;
    std::vector<XrView> views(ctx.vr.swapchains.size(), {XR_TYPE_VIEW});
    xrCheckResult(xrLocateViews(ctx.vr.session, &locate_info, &view_state, (uint32_t)views.size(), &view_count, views.data()));


    for(uint32_t v = 0; v < ctx.vr.swapchains.size(); v++) {
        auto& view = views[v];

        ubo.views[v].pointers[0]->view_projection = projection_fov(views[v].fov, 0.01, 100.) * view_pose(view.pose);
        ubo.views[v].pointers[0]->position = glm::vec4(view.pose.position.x, -view.pose.position.y, view.pose.position.z, 1.0f);

    }

    // Submit command buffer

    auto stages = std::vector<vk::PipelineStageFlags> {vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe};

    ctx.device.graphics.submit({vk::SubmitInfo(
            Util::removeElement<vk::Semaphore>(waits, nullptr), waits.data(), stages.data(),
            1, &commandBuffers[frame_index],
            Util::removeElement<vk::Semaphore>(signals, nullptr), signals.data()
    )}, fence);




    std::vector<XrCompositionLayerProjectionView> proj_views(ctx.vr.swapchains.size());

    for(uint32_t v = 0; v < ctx.vr.swapchains.size(); v++) {
        auto& view = views[v];

        proj_views[v] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        proj_views[v].pose = view.pose;
        proj_views[v].fov = view.fov;
        proj_views[v].subImage.swapchain = ctx.vr.swapchains[v].handle;
        proj_views[v].subImage.imageRect.offset = {0, 0};
        proj_views[v].subImage.imageRect.extent = {(int32_t)ctx.vr.swapchains[v].width, (int32_t)ctx.vr.swapchains[v].height};

    }



    // Wait on fence on separate thread to avoid wasting time but still present as soon as possible

    taskflow.emplace([this, proj_views, predicted_time]() {
    
        ctx.device->waitForFences(fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
        ctx.device->resetFences(fence);


        for(uint32_t v = 0; v < ctx.vr.swapchains.size(); v++) {
            xrCheckResult(xrReleaseSwapchainImage(ctx.vr.swapchains[v].handle, nullptr));
        }

        XrCompositionLayerProjection layer_proj = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer_proj.space = ctx.vr.space;
        layer_proj.viewCount = (uint32_t)proj_views.size();
        layer_proj.views = proj_views.data();

        XrCompositionLayerBaseHeader* layer = reinterpret_cast<XrCompositionLayerBaseHeader*> (&layer_proj);

        XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
        end_info.displayTime = predicted_time;
        end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end_info.layerCount = 1;
        end_info.layers = &layer;
        xrCheckResult(xrEndFrame(ctx.vr.session, &end_info));
        
    });

    tf::Executor& executor = reg.ctx<tf::Executor>();
    future = executor.run(taskflow);
    
}



VRRender::~VRRender() {

    ctx.device->destroy(fence);
    
    ctx.device->destroy(commandPool);
    
}
