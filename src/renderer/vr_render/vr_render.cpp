#include "vr_render.h"

#include "renderer/tree_shade.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "util/log.h"

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"

#include "view/components/input.h"
#include "logic/components/vr.h"

using namespace dy;

constexpr float vr_near = 0.05f;
constexpr float vr_far = 3000.f;

VRRender::VRRender(Context& ctx, entt::registry& reg) : reg(reg), ctx(ctx), renderpass(ctx),
per_frame(NUM_FRAMES) {

    reg.ctx().emplace<VRRig>();

    // Also registers the VRInput context variable when VR is disabled
    actions = std::make_unique<VRActions>(ctx, reg);

    if(!enabled()) {
        return;
    }

    uint32_t num_views = ctx.vr.num_views;
    views.resize(num_views, { XR_TYPE_VIEW });
    image_indices.resize(num_views);

    commandPool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, ctx.device.g_i));

    auto cmds = ctx.device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, (uint32_t) per_frame.size()));

    auto sizes = std::vector {
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, (uint32_t) per_frame.size() * num_views),
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, (uint32_t) per_frame.size() * num_views * 2),
    };
    descriptorPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, (uint32_t) per_frame.size() * num_views, sizes));

    for(size_t i = 0; i < per_frame.size(); i++) {
        auto& f = per_frame[i];
        f.commandBuffer = cmds[i];
        f.eyes.resize(num_views);
        for(auto& eye : f.eyes) {
            eye.set = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descriptorPool, 1, &ctx.view_layout))[0];

            VmaAllocationCreateInfo allocInfo = {};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            eye.ubo = VmaBuffer(ctx.device, &allocInfo, vk::BufferCreateInfo({}, sizeof(ViewUBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));

            VmaAllocationInfo inf;
            vmaGetAllocationInfo(ctx.device, eye.ubo.allocation, &inf);
            eye.pointer = static_cast<ViewUBO*> (inf.pMappedData);

            auto bufInfo = vk::DescriptorBufferInfo(eye.ubo, 0, sizeof(ViewUBO));
            auto soilInfo = vk::DescriptorImageInfo(ctx.soil_sampler, ctx.soil_view, vk::ImageLayout::eShaderReadOnlyOptimal);

            ctx.device->updateDescriptorSets({
                vk::WriteDescriptorSet(eye.set, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo, nullptr),
                vk::WriteDescriptorSet(eye.set, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &soilInfo, nullptr, nullptr)
                }, {});
        }
    }

}

bool VRRender::enabled() const {
    return ctx.vr.enabled();
}

// Vulkan clip space projection from an asymmetric OpenXR field of view (Y down, depth in [0, 1])
static glm::mat4 projection_fov(const XrFovf fov, const float near_z, const float far_z) {

    const float tan_left = tanf(fov.angleLeft);
    const float tan_right = tanf(fov.angleRight);
    const float tan_down = tanf(fov.angleDown);
    const float tan_up = tanf(fov.angleUp);

    const float tan_width = tan_right - tan_left;
    const float tan_height = tan_down - tan_up;

    glm::mat4 m(0.f);
    m[0][0] = 2.f / tan_width;
    m[2][0] = (tan_right + tan_left) / tan_width;
    m[1][1] = 2.f / tan_height;
    m[2][1] = (tan_up + tan_down) / tan_height;
    m[2][2] = -far_z / (far_z - near_z);
    m[3][2] = -(far_z * near_z) / (far_z - near_z);
    m[2][3] = -1.f;
    return m;

}

static glm::mat4 pose_matrix(const XrPosef& pose) {
    glm::quat rotation(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
    return glm::translate(glm::mat4(1.f), glm::vec3(pose.position.x, pose.position.y, pose.position.z)) * glm::mat4_cast(rotation);
}

void VRRender::pollEvents() {

    auto& vr_input = reg.ctx().get<VRInput>();

    XrEventDataBuffer event { XR_TYPE_EVENT_DATA_BUFFER };

    while(xrPollEvent(ctx.vr.instance, &event) == XR_SUCCESS) {

        if(event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {

            auto* changed = reinterpret_cast<XrEventDataSessionStateChanged*>(&event);

            switch(changed->state) {
                case XR_SESSION_STATE_READY: {
                    XrSessionBeginInfo begin_info { XR_TYPE_SESSION_BEGIN_INFO };
                    begin_info.primaryViewConfigurationType = ctx.vr.view_type;
                    if(xrCheckResult(xrBeginSession(ctx.vr.session, &begin_info), "xrBeginSession")) {
                        session_running = true;
                        dy::log(dy::info) << "VR session started\n";
                    }
                    break;
                }
                case XR_SESSION_STATE_STOPPING:
                    xrCheckResult(xrEndSession(ctx.vr.session), "xrEndSession");
                    session_running = false;
                    dy::log(dy::info) << "VR session stopped\n";
                    break;
                case XR_SESSION_STATE_EXITING:
                case XR_SESSION_STATE_LOSS_PENDING:
                    session_running = false;
                    // The headset application was closed : keep the desktop running
                    dy::log(dy::warning) << "VR session lost\n";
                    break;
                default:
                    break;
            }

            vr_input.focused = changed->state == XR_SESSION_STATE_FOCUSED;

        } else if(event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            session_running = false;
            dy::log(dy::warning) << "VR runtime is shutting down\n";
        }

        event = { XR_TYPE_EVENT_DATA_BUFFER };
    }

    vr_input.session_running = session_running;

}

void VRRender::prepare(uint32_t frame_index) {

    frame_begun = false;
    should_render = false;

    if(!enabled()) {
        return;
    }

    OPTICK_EVENT();

    pollEvents();

    if(!session_running) {
        return;
    }

    {
        OPTICK_EVENT("wait_frame");
        frame_state = { XR_TYPE_FRAME_STATE };
        if(!xrCheckResult(xrWaitFrame(ctx.vr.session, nullptr, &frame_state), "xrWaitFrame")) {
            return;
        }
    }

    XrResult result = xrBeginFrame(ctx.vr.session, nullptr);
    if(XR_FAILED(result)) {
        xrCheckResult(result, "xrBeginFrame");
        return;
    }

    frame_begun = true;
    should_render = frame_state.shouldRender == XR_TRUE;

    actions->sync(frame_state.predictedDisplayTime);

}

void VRRender::acquire() {

    if(!active()) return;

    OPTICK_EVENT();

    for(uint32_t v = 0; v < ctx.vr.swapchains.size(); v++) {
        auto handle = ctx.vr.swapchains[v].handle;
        xrCheckResult(xrAcquireSwapchainImage(handle, nullptr, &image_indices[v]));

        XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wait_info.timeout = XR_INFINITE_DURATION;
        xrCheckResult(xrWaitSwapchainImage(handle, &wait_info));
    }

}

void VRRender::updateView(uint32_t frame_index, float time) {

    if(!active()) return;

    OPTICK_EVENT();

    // Located as late as possible for the freshest head pose
    uint32_t view_count = ctx.vr.num_views;
    XrViewState view_state = { XR_TYPE_VIEW_STATE };
    XrViewLocateInfo locate_info = { XR_TYPE_VIEW_LOCATE_INFO };
    locate_info.viewConfigurationType = ctx.vr.view_type;
    locate_info.displayTime = frame_state.predictedDisplayTime;
    locate_info.space = ctx.vr.space;
    for(auto& view : views) view = { XR_TYPE_VIEW };
    xrCheckResult(xrLocateViews(ctx.vr.session, &locate_info, &view_state, (uint32_t) views.size(), &view_count, views.data()));

    glm::mat4 world_from_stage = reg.ctx().get<VRRig>().transform();

    for(uint32_t v = 0; v < ctx.vr.num_views; v++) {
        glm::mat4 world_from_eye = world_from_stage * pose_matrix(views[v].pose);
        auto* ubo = per_frame[frame_index].eyes[v].pointer;
        ubo->view_projection = projection_fov(views[v].fov, vr_near, vr_far) * glm::inverse(world_from_eye);
        ubo->position = glm::vec4(glm::vec3(world_from_eye[3]), time);
        ubo->tree_shade = treeShadeView(reg);

        auto image = vk::DescriptorImageInfo(ctx.tree_shade_sampler, ctx.tree_shade_view, vk::ImageLayout::eShaderReadOnlyOptimal);
        ctx.device->updateDescriptorSets({
            vk::WriteDescriptorSet(per_frame[frame_index].eyes[v].set, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image, nullptr, nullptr)
            }, {});
    }

}

void VRRender::record(uint32_t frame_index, const std::vector<ScenePass*>& passes) {

    OPTICK_EVENT();

    auto& f = per_frame[frame_index];
    auto command = f.commandBuffer;

    command.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr));

    for(uint32_t v = 0; v < ctx.vr.swapchains.size(); v++) {
        auto& swapchain = ctx.vr.swapchains[v];
        vk::Extent2D extent(swapchain.width, swapchain.height);

        auto clearValues = std::vector<vk::ClearValue>{
                vk::ClearValue(vk::ClearColorValue(std::array<float, 4> { 0.45f, 0.6f, 0.8f, 1.0f })),
                vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0)) };

        command.beginRenderPass(vk::RenderPassBeginInfo(renderpass, renderpass.views[v].framebuffers[image_indices[v]], vk::Rect2D({}, extent), clearValues), vk::SubpassContents::eInline);

        command.setViewport(0, vk::Viewport(0.f, 0.f, (float) extent.width, (float) extent.height, 0.f, 1.f));
        command.setScissor(0, vk::Rect2D(vk::Offset2D(), extent));

        RenderView view { RenderTarget::VR, frame_index, v, f.eyes[v].set, extent };

        for(ScenePass* pass : passes) {
            if(pass->supports(RenderTarget::VR)) {
                pass->record(command, view);
            }
        }

        command.endRenderPass();
    }

    command.end();

}

void VRRender::endFrame() {

    if(!frame_begun) return;

    OPTICK_EVENT();

    std::vector<XrCompositionLayerProjectionView> proj_views;
    XrCompositionLayerProjection layer_proj = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    XrCompositionLayerBaseHeader* layer = reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer_proj);

    XrFrameEndInfo end_info { XR_TYPE_FRAME_END_INFO };
    end_info.displayTime = frame_state.predictedDisplayTime;
    end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    if(should_render) {

        for(auto& swapchain : ctx.vr.swapchains) {
            XrSwapchainImageReleaseInfo release_info { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            xrCheckResult(xrReleaseSwapchainImage(swapchain.handle, &release_info));
        }

        proj_views.resize(ctx.vr.num_views);
        for(uint32_t v = 0; v < ctx.vr.num_views; v++) {
            proj_views[v] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            proj_views[v].pose = views[v].pose;
            proj_views[v].fov = views[v].fov;
            proj_views[v].subImage.swapchain = ctx.vr.swapchains[v].handle;
            proj_views[v].subImage.imageRect.offset = {0, 0};
            proj_views[v].subImage.imageRect.extent = {(int32_t) ctx.vr.swapchains[v].width, (int32_t) ctx.vr.swapchains[v].height};
        }

        layer_proj.space = ctx.vr.space;
        layer_proj.viewCount = (uint32_t) proj_views.size();
        layer_proj.views = proj_views.data();

        end_info.layerCount = 1;
        end_info.layers = &layer;

    }

    xrCheckResult(xrEndFrame(ctx.vr.session, &end_info), "xrEndFrame");

    frame_begun = false;
    should_render = false;

}

VRRender::~VRRender() {

    actions.reset();

    if(!enabled()) {
        return;
    }

    if(session_running) {
        xrRequestExitSession(ctx.vr.session);
    }

    ctx.device->destroy(commandPool);
    ctx.device->destroy(descriptorPool);

}
