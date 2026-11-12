#include "sun_visibility.h"

#include "renderer/context/context.h"
#include "renderer/lighting/ray_scene.h"
#include "renderer/util/pipeline.h"

using namespace dy;

namespace {

const vk::Format colour_format = vk::Format::eR16G16B16A16Sfloat;
const vk::Format sun_format = vk::Format::eB10G11R11UfloatPack32;
const vk::Format visibility_format = vk::Format::eR16G16B16A16Sfloat;

// Push constants of sun_visibility.rt.frag.glsl and composite.frag.glsl
struct VisibilityPC {
    glm::vec2 texel;        // 1 / size of the images
    float history;          // frames the history holds, 0 : none
    float pad;
};

}

SunVisibility::Target SunVisibility::makeTarget(vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect) {
    Target made;
    VmaAllocationCreateInfo alloc {};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    made.image = VmaImage(ctx.device, &alloc, vk::ImageCreateInfo({}, vk::ImageType::e2D, format, vk::Extent3D(extent.width, extent.height, 1),
        1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, usage | vk::ImageUsageFlagBits::eSampled,
        vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined));
    made.view = ctx.device->createImageView(vk::ImageViewCreateInfo({}, made.image.image, vk::ImageViewType::e2D, format,
        vk::ComponentMapping(), vk::ImageSubresourceRange(aspect, 0, 1, 0, 1)));
    return made;
}

SunVisibility::SunVisibility(Context& ctx, entt::registry& reg, RenderTarget target, const std::vector<vk::Extent2D>& extents, vk::RenderPass final_pass)
    : ctx(ctx), reg(reg), target(target) {

    Device& device = ctx.device;

    // With a stencil where the GPU has one, for the scene passes (Context::sceneDepthFormat)
    depth_format = ctx.sceneDepthFormat();

    // The pass of the scene : colour, sun and depth, all read afterwards
    {
        std::vector<vk::AttachmentDescription> attachments {
            vk::AttachmentDescription({}, colour_format, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal),
            vk::AttachmentDescription({}, sun_format, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal),
            vk::AttachmentDescription({}, depth_format, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilReadOnlyOptimal),
        };
        std::vector<vk::AttachmentReference> colours {
            vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal),
            vk::AttachmentReference(1, vk::ImageLayout::eColorAttachmentOptimal),
        };
        vk::AttachmentReference depth(2, vk::ImageLayout::eDepthStencilAttachmentOptimal);
        vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colours, {}, &depth);

        auto attachment_stages = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
        auto attachment_access = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        std::vector<vk::SubpassDependency> dependencies {
            // After the frame before read them
            vk::SubpassDependency(VK_SUBPASS_EXTERNAL, 0, attachment_stages | vk::PipelineStageFlagBits::eFragmentShader, attachment_stages,
                attachment_access | vk::AccessFlagBits::eShaderRead, attachment_access),
            vk::SubpassDependency(0, VK_SUBPASS_EXTERNAL, attachment_stages, vk::PipelineStageFlagBits::eFragmentShader,
                attachment_access, vk::AccessFlagBits::eShaderRead),
        };
        scene_pass = device->createRenderPass(vk::RenderPassCreateInfo({}, attachments, subpass, dependencies));
    }

    // The pass of the visibility : one image, every pixel written
    {
        vk::AttachmentDescription attachment({}, visibility_format, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
        vk::AttachmentReference colour(0, vk::ImageLayout::eColorAttachmentOptimal);
        vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colour);
        std::vector<vk::SubpassDependency> dependencies {
            vk::SubpassDependency(VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader,
                vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eColorAttachmentWrite),
            vk::SubpassDependency(0, VK_SUBPASS_EXTERNAL, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader,
                vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead),
        };
        visibility_pass = device->createRenderPass(vk::RenderPassCreateInfo({}, attachment, subpass, dependencies));
    }

    nearest = device->createSampler(vk::SamplerCreateInfo({}, vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge));
    linear = device->createSampler(vk::SamplerCreateInfo({}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge));

    // Set 1 of both pipelines : depth, colour, sun, visibility
    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
    };
    images_layout = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));

    uint32_t sets = (uint32_t) extents.size() * 5;
    auto sizes = std::vector { vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, sets * 4) };
    pool = device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, sets, sizes));

    auto layouts = std::vector { ctx.view_layout, images_layout };
    auto range = vk::PushConstantRange(vk::ShaderStageFlagBits::eFragment, 0, sizeof(VisibilityPC));
    layout = device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, range));

    {
        GraphicsPipelineDesc desc;
        desc.vertex_shader = "./resources/shaders/fullscreen.vert.glsl.spv";
        desc.fragment_shader = "./resources/shaders/sun_visibility.rt.frag.glsl.spv";
        desc.layout = layout;
        desc.renderpass = visibility_pass;
        desc.cull = vk::CullModeFlagBits::eNone;
        desc.depth_test = false;
        desc.depth_write = false;
        trace_pipeline = createGraphicsPipeline(device, desc);

        desc.fragment_shader = "./resources/shaders/sun_resolve.frag.glsl.spv";
        resolve_pipeline = createGraphicsPipeline(device, desc);

        desc.fragment_shader = "./resources/shaders/composite.frag.glsl.spv";
        desc.renderpass = final_pass;
        composite_pipeline = createGraphicsPipeline(device, desc);
    }

    vk::CommandBuffer setup = ctx.transfer.getCommandBuffer();

    eyes.resize(extents.size());
    for(size_t e = 0; e < eyes.size(); e++) {
        Eye& eye = eyes[e];
        eye.extent = extents[e];
        eye.colour = makeTarget(eye.extent, colour_format, vk::ImageUsageFlagBits::eColorAttachment, vk::ImageAspectFlagBits::eColor);
        eye.sun = makeTarget(eye.extent, sun_format, vk::ImageUsageFlagBits::eColorAttachment, vk::ImageAspectFlagBits::eColor);
        eye.depth = makeTarget(eye.extent, depth_format, vk::ImageUsageFlagBits::eDepthStencilAttachment, Context::depthAspects(depth_format));
        // The shaders read the depth alone
        eye.depth_read = device->createImageView(vk::ImageViewCreateInfo({}, eye.depth.image.image, vk::ImageViewType::e2D, depth_format,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1)));

        auto scene_views = std::vector { eye.colour.view, eye.sun.view, eye.depth.view };
        eye.scene = device->createFramebuffer(vk::FramebufferCreateInfo({}, scene_pass, scene_views, eye.extent.width, eye.extent.height, 1));

        eye.rays = makeTarget(eye.extent, visibility_format, vk::ImageUsageFlagBits::eColorAttachment, vk::ImageAspectFlagBits::eColor);
        eye.trace = device->createFramebuffer(vk::FramebufferCreateInfo({}, visibility_pass, eye.rays.view, eye.extent.width, eye.extent.height, 1));

        for(int i = 0; i < 2; i++) {
            eye.visibility[i] = makeTarget(eye.extent, visibility_format, vk::ImageUsageFlagBits::eColorAttachment, vk::ImageAspectFlagBits::eColor);
            eye.resolve[i] = device->createFramebuffer(vk::FramebufferCreateInfo({}, visibility_pass, eye.visibility[i].view, eye.extent.width, eye.extent.height, 1));

            // The first frame reads the other image as its history (and gives it no weight)
            setup.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {},
                vk::ImageMemoryBarrier({}, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, eye.visibility[i].image.image, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
        }

        auto depth = vk::DescriptorImageInfo(nearest, eye.depth_read, vk::ImageLayout::eDepthStencilReadOnlyOptimal);
        auto colour = vk::DescriptorImageInfo(nearest, eye.colour.view, vk::ImageLayout::eShaderReadOnlyOptimal);
        auto sun = vk::DescriptorImageInfo(nearest, eye.sun.view, vk::ImageLayout::eShaderReadOnlyOptimal);
        auto rays = vk::DescriptorImageInfo(nearest, eye.rays.view, vk::ImageLayout::eShaderReadOnlyOptimal);

        // Bindings : depth, colour (the rays found for the resolve), sun, visibility (the history for the resolve)
        auto write = [&](vk::DescriptorSet set, const vk::DescriptorImageInfo& second, const vk::DescriptorImageInfo& fourth) {
            device->updateDescriptorSets({
                vk::WriteDescriptorSet(set, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &depth),
                vk::WriteDescriptorSet(set, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &second),
                vk::WriteDescriptorSet(set, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &sun),
                vk::WriteDescriptorSet(set, 3, 0, 1, vk::DescriptorType::eCombinedImageSampler, &fourth),
            }, {});
        };

        auto set_layouts = std::vector { images_layout, images_layout, images_layout, images_layout, images_layout };
        auto made = device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, set_layouts));
        eye.trace_set = made[0];
        write(eye.trace_set, colour, sun);
        for(int i = 0; i < 2; i++) {
            auto before = vk::DescriptorImageInfo(linear, eye.visibility[1 - i].view, vk::ImageLayout::eShaderReadOnlyOptimal);
            auto now = vk::DescriptorImageInfo(nearest, eye.visibility[i].view, vk::ImageLayout::eShaderReadOnlyOptimal);
            eye.resolve_set[i] = made[1 + i];
            eye.composite_set[i] = made[3 + i];
            write(eye.resolve_set[i], rays, before);
            write(eye.composite_set[i], colour, now);
        }
    }

}

void SunVisibility::beginFrame() {
    current = 1 - current;
    history++;
}

void SunVisibility::beginScene(vk::CommandBuffer command, uint32_t eye_index) {

    Eye& eye = eyes[eye_index];

    // The colour is cleared to nothing : alpha 1 is "the ray starts on the surface". No sun on the sky
    auto clears = std::vector<vk::ClearValue> {
        vk::ClearValue(vk::ClearColorValue(std::array<float, 4> { 0.f, 0.f, 0.f, 1.f })),
        vk::ClearValue(vk::ClearColorValue(std::array<float, 4> { 0.f, 0.f, 0.f, 0.f })),
        vk::ClearValue(vk::ClearDepthStencilValue(1.f, 0)),
    };
    command.beginRenderPass(vk::RenderPassBeginInfo(scene_pass, eye.scene, vk::Rect2D({}, eye.extent), clears), vk::SubpassContents::eInline);
    command.setViewport(0, vk::Viewport(0.f, 0.f, (float) eye.extent.width, (float) eye.extent.height, 0.f, 1.f));
    command.setScissor(0, vk::Rect2D(vk::Offset2D(), eye.extent));

}

void SunVisibility::endScene(vk::CommandBuffer command) {
    command.endRenderPass();
}

void SunVisibility::fullscreen(vk::CommandBuffer command, vk::Extent2D extent, vk::Framebuffer framebuffer, vk::Pipeline pipeline,
    vk::DescriptorSet view_set, vk::DescriptorSet images) {

    if(framebuffer) {
        command.beginRenderPass(vk::RenderPassBeginInfo(visibility_pass, framebuffer, vk::Rect2D({}, extent), {}), vk::SubpassContents::eInline);
        command.setViewport(0, vk::Viewport(0.f, 0.f, (float) extent.width, (float) extent.height, 0.f, 1.f));
        command.setScissor(0, vk::Rect2D(vk::Offset2D(), extent));
    }

    // The first frame has no history, and the one after a history of one
    VisibilityPC pc { glm::vec2(1.f / (float) extent.width, 1.f / (float) extent.height), (float) std::min(history - 1, 1000u), 0.f };
    command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, { view_set, images }, {});
    command.pushConstants(layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(VisibilityPC), &pc);
    command.draw(3, 1, 0, 0);

    if(framebuffer) command.endRenderPass();

}

void SunVisibility::trace(vk::CommandBuffer command, uint32_t eye_index, vk::DescriptorSet view_set) {
    Eye& eye = eyes[eye_index];
    fullscreen(command, eye.extent, eye.trace, trace_pipeline, view_set, eye.trace_set);
    fullscreen(command, eye.extent, eye.resolve[current], resolve_pipeline, view_set, eye.resolve_set[current]);
}

void SunVisibility::composite(vk::CommandBuffer command, uint32_t eye_index, vk::DescriptorSet view_set) {
    Eye& eye = eyes[eye_index];
    fullscreen(command, eye.extent, nullptr, composite_pipeline, view_set, eye.composite_set[current]);
}

SunVisibility::~SunVisibility() {

    Device& device = ctx.device;
    device->waitIdle();

    for(auto& eye : eyes) {
        device->destroy(eye.scene);
        device->destroy(eye.trace);
        for(auto framebuffer : eye.resolve) device->destroy(framebuffer);
        device->destroy(eye.rays.view);
        device->destroy(eye.colour.view);
        device->destroy(eye.sun.view);
        device->destroy(eye.depth_read);
        device->destroy(eye.depth.view);
        for(auto& visibility : eye.visibility) device->destroy(visibility.view);
    }

    device->destroy(trace_pipeline);
    device->destroy(resolve_pipeline);
    device->destroy(composite_pipeline);
    device->destroy(layout);
    device->destroy(pool);
    device->destroy(images_layout);
    device->destroy(nearest);
    device->destroy(linear);
    device->destroy(visibility_pass);
    device->destroy(scene_pass);

}
