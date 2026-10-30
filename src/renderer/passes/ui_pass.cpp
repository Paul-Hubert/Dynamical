#include "ui_pass.h"

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"
#include "renderer/util/pipeline.h"

#include <cstring>

using namespace dy;

static void resize(Context& ctx, VmaBuffer& buffer, void*& pointer, size_t size, vk::BufferUsageFlags usage) {

    if(buffer && buffer.size >= size) return;

    VmaAllocationCreateInfo info {};
    info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    // Grow by half to avoid reallocating every frame while windows open
    buffer = VmaBuffer(ctx.device, &info, vk::BufferCreateInfo({}, size + size / 2, usage, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));

    VmaAllocationInfo inf;
    vmaGetAllocationInfo(ctx.device, buffer.allocation, &inf);
    pointer = inf.pMappedData;

}

UIPass::UIPass(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg), frames(NUM_FRAMES) {

    if(!ImGui::GetCurrentContext()) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
    }

    auto& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    io.IniFilename = nullptr;

    unsigned char* tex_pixels = nullptr;
    int tex_w, tex_h;
    io.Fonts->GetTexDataAsRGBA32(&tex_pixels, &tex_w, &tex_h);

    {
        vk::ImageCreateInfo info(
            {}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Unorm, vk::Extent3D(tex_w, tex_h, 1), 1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
        fontAtlas = ctx.transfer.createImage(tex_pixels, (size_t) tex_w * tex_h * 4, info, vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    fontView = ctx.device->createImageView(vk::ImageViewCreateInfo(
        {}, fontAtlas->image, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Unorm, {},
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
    ));

    fontSampler = ctx.device->createSampler(vk::SamplerCreateInfo(
        {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge
    ));

    auto poolSizes = std::vector {
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1),
    };
    descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, poolSizes));

    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
    };
    descLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));
    descSet = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &descLayout))[0];

    auto imageInfo = vk::DescriptorImageInfo(fontSampler, fontView, vk::ImageLayout::eShaderReadOnlyOptimal);
    ctx.device->updateDescriptorSets({vk::WriteDescriptorSet(descSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr, nullptr)}, {});

    auto range = vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, 4 * sizeof(float));
    layout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, descLayout, range));

}

void UIPass::createPipeline(RenderTarget target, vk::RenderPass renderpass) {

    GraphicsPipelineDesc desc;
    desc.vertex_shader = "./resources/shaders/uirender.vert.glsl.spv";
    desc.fragment_shader = "./resources/shaders/uirender.frag.glsl.spv";
    desc.bindings = {
        vk::VertexInputBindingDescription(0, sizeof(ImDrawVert), vk::VertexInputRate::eVertex),
    };
    desc.attributes = {
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, pos)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, uv)),
        vk::VertexInputAttributeDescription(2, 0, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col)),
    };
    desc.layout = layout;
    desc.renderpass = renderpass;
    desc.cull = vk::CullModeFlagBits::eNone;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.alpha_blend = true;

    pipelines[index(target)] = createGraphicsPipeline(ctx.device, desc);

}

void UIPass::update(uint32_t frame_index) {

    OPTICK_EVENT();

    ImGui::Render();

    draw_data = ImGui::GetDrawData();
    if(!draw_data || draw_data->TotalVtxCount == 0) {
        draw_data = nullptr;
        return;
    }

    auto& fd = frames[frame_index];

    size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
    size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

    resize(ctx, fd.vertices, fd.vertex_pointer, vertex_size, vk::BufferUsageFlagBits::eVertexBuffer);
    resize(ctx, fd.indices, fd.index_pointer, index_size, vk::BufferUsageFlagBits::eIndexBuffer);

    auto* vtx_dst = static_cast<ImDrawVert*>(fd.vertex_pointer);
    auto* idx_dst = static_cast<ImDrawIdx*>(fd.index_pointer);

    for(int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        std::memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        std::memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += cmd_list->VtxBuffer.Size;
        idx_dst += cmd_list->IdxBuffer.Size;
    }

}

void UIPass::record(vk::CommandBuffer command, const RenderView& view) {

    if(!draw_data) return;

    int fb_width = (int) (draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int) (draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if(fb_width <= 0 || fb_height <= 0) return;

    auto& fd = frames[view.frame_index];

    command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines[index(view.target)]);
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {descSet}, {});
    command.bindVertexBuffers(0, {fd.vertices.buffer}, {0});
    command.bindIndexBuffer(fd.indices.buffer, 0, sizeof(ImDrawIdx) == 2 ? vk::IndexType::eUint16 : vk::IndexType::eUint32);

    command.setViewport(0, vk::Viewport(0, 0, (float) fb_width, (float) fb_height, 0.0f, 1.0f));

    float scale[4];
    scale[0] = 2.0f / draw_data->DisplaySize.x;
    scale[1] = 2.0f / draw_data->DisplaySize.y;
    scale[2] = -1.0f - draw_data->DisplayPos.x * scale[0];
    scale[3] = -1.0f - draw_data->DisplayPos.y * scale[1];
    command.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(float) * 4, scale);

    ImVec2 clip_off = draw_data->DisplayPos;
    ImVec2 clip_scale = draw_data->FramebufferScale;

    int vtx_offset = 0;
    int idx_offset = 0;
    for(int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for(int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if(pcmd->UserCallback) {
                continue;
            }

            if(pcmd->TextureId != nullptr) {
                command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {*static_cast<vk::DescriptorSet*>(pcmd->TextureId)}, {});
            }

            ImVec4 clip_rect;
            clip_rect.x = std::max((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, 0.f);
            clip_rect.y = std::max((pcmd->ClipRect.y - clip_off.y) * clip_scale.y, 0.f);
            clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
            clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;

            if(clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z > clip_rect.x && clip_rect.w > clip_rect.y) {
                command.setScissor(0, vk::Rect2D(vk::Offset2D((int32_t) clip_rect.x, (int32_t) clip_rect.y),
                                                 vk::Extent2D((uint32_t) (clip_rect.z - clip_rect.x), (uint32_t) (clip_rect.w - clip_rect.y))));
                command.drawIndexed(pcmd->ElemCount, 1, pcmd->IdxOffset + idx_offset, pcmd->VtxOffset + vtx_offset, 0);
            }

            if(pcmd->TextureId != nullptr) {
                command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {descSet}, {});
            }
        }
        idx_offset += cmd_list->IdxBuffer.Size;
        vtx_offset += cmd_list->VtxBuffer.Size;
    }

    // Restore the full scissor for whatever records after the UI
    command.setScissor(0, vk::Rect2D(vk::Offset2D(), view.extent));

}

UIPass::~UIPass() {

    for(auto pipeline : pipelines) {
        if(pipeline) ctx.device->destroy(pipeline);
    }
    ctx.device->destroy(layout);
    ctx.device->destroy(descLayout);
    ctx.device->destroy(descPool);
    ctx.device->destroy(fontSampler);
    ctx.device->destroy(fontView);

}
