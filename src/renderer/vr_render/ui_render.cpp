#include "ui_render.h"

#include "renderer/context/context.h"
#include "renderpass.h"

#include "imgui.h"

#include "util/util.h"

UIRender::UIRender(Context& ctx, Renderpass& renderpass) : ctx(ctx), g_FramesDataBuffers(ctx.swap.num_frames) {
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    auto& io = ImGui::GetIO(); (void)io;
    io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("./resources/fonts/aniron.ttf", 50.0f, NULL, io.Fonts->GetGlyphRangesDefault());

    io.IniFilename = nullptr;

    
    unsigned char* tex_pixels = NULL;
    int tex_w, tex_h;
    io.Fonts->GetTexDataAsRGBA32(&tex_pixels, &tex_w, &tex_h);
    
    {
        vk::ImageCreateInfo info(
            {}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Unorm, vk::Extent3D(tex_w, tex_h, 1), 1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
        fontAtlas = ctx.transfer.createImage(tex_pixels, tex_w * tex_h * 4, info, vk::ImageLayout::eShaderReadOnlyOptimal);
    }
    
    fontView = ctx.device->createImageView(vk::ImageViewCreateInfo(
        {}, fontAtlas->image, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Unorm, {}, 
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
    ));
    
    fontSampler = ctx.device->createSampler(vk::SamplerCreateInfo(
        {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge
    ));
    
    {
        auto poolSizes = std::vector {
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1),
        };
        descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, (uint32_t) poolSizes.size(), poolSizes.data()));
    }
    
    {
        auto bindings = std::vector {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
        };
        descLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, (uint32_t) bindings.size(), bindings.data()));
        
        descSet = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &descLayout))[0];
        
    }
    
    {
        auto info = vk::DescriptorImageInfo(fontSampler, fontView, vk::ImageLayout::eShaderReadOnlyOptimal);

        ctx.device->updateDescriptorSets({vk::WriteDescriptorSet(descSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &info, 0, 0)}, {});
    }

    initPipeline(renderpass);
    
}

void UIRender::createOrResizeBuffer(vk::Buffer& buffer, vk::DeviceMemory& buffer_memory, vk::DeviceSize& p_buffer_size, size_t new_size, vk::BufferUsageFlagBits usage) {
    
    if (buffer)
        ctx.device->destroyBuffer(buffer);
    if (buffer_memory)
        ctx.device->freeMemory(buffer_memory);
    
    buffer = ctx.device->createBuffer(vk::BufferCreateInfo({}, new_size, usage, vk::SharingMode::eExclusive));

    vk::MemoryRequirements req = ctx.device->getBufferMemoryRequirements(buffer);
    
    buffer_memory = ctx.device->allocateMemory(
        vk::MemoryAllocateInfo(req.size,
            ctx.device.getMemoryType(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
    
    ctx.device->bindBufferMemory(buffer, buffer_memory, 0);
    p_buffer_size = new_size;
    
}

void UIRender::render(vk::CommandBuffer commandBuffer, uint32_t index) {

    ImGui::Render();
    entt::monostate<"imgui_frame"_hs>{} = false;
    
    ImDrawData* draw_data = ImGui::GetDrawData();
    
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0 || draw_data->TotalVtxCount == 0)
        return;

    FrameDataForRender* fd = &g_FramesDataBuffers[index];

    // Create the Vertex and Index buffers:
    size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
    size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
    
    if (!fd->vertexBuffer || fd->vertexBufferSize < vertex_size)
        createOrResizeBuffer(fd->vertexBuffer, fd->vertexBufferMemory, fd->vertexBufferSize, vertex_size, vk::BufferUsageFlagBits::eVertexBuffer);
    if (!fd->indexBuffer || fd->indexBufferSize < index_size)
        createOrResizeBuffer(fd->indexBuffer, fd->indexBufferMemory, fd->indexBufferSize, index_size, vk::BufferUsageFlagBits::eIndexBuffer);

    // Upload Vertex and index Data:
    {
        ImDrawVert* vtx_dst = (ImDrawVert*)ctx.device->mapMemory(fd->vertexBufferMemory, 0, vertex_size, {});
        ImDrawIdx* idx_dst = (ImDrawIdx*)ctx.device->mapMemory(fd->indexBufferMemory, 0, index_size, {});
        
        for (int n = 0; n < draw_data->CmdListsCount; n++)
        {
            const ImDrawList* cmd_list = draw_data->CmdLists[n];
            memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
            memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
            vtx_dst += cmd_list->VtxBuffer.Size;
            idx_dst += cmd_list->IdxBuffer.Size;
        }
        
        ctx.device->unmapMemory(fd->vertexBufferMemory);
        ctx.device->unmapMemory(fd->indexBufferMemory);
    }
    
    
    
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
    
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, {descSet}, {});
    
    
    commandBuffer.bindVertexBuffers(0, {fd->vertexBuffer}, {0});
    commandBuffer.bindIndexBuffer(fd->indexBuffer, 0, vk::IndexType::eUint16);

    commandBuffer.setViewport(0, vk::Viewport(0, 0, (float)fb_width, (float)fb_height, 0.0f, 1.0f));
    

    // Setup scale and translation:
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayMin is typically (0,0) for single viewport apps.
    
    float scale[4];
    scale[0] = 2.0f / draw_data->DisplaySize.x;
    scale[1] = 2.0f / draw_data->DisplaySize.y;
    scale[2] = -1.0f - draw_data->DisplayPos.x * scale[0];
    scale[3] = -1.0f - draw_data->DisplayPos.y * scale[1];
    commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex, sizeof(float) * 0, sizeof(float) * 4, scale);
    

    // Will project scissor/clipping rectangles into framebuffer space
    ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
    ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    // Render command lists
    int vtx_offset = 0;
    int idx_offset = 0;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                if(pcmd->TextureId != NULL) {
                    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, {*static_cast<vk::DescriptorSet*>(pcmd->TextureId)}, {});
                }
                // Project scissor/clipping rectangles into framebuffer space
                ImVec4 clip_rect;
                clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
                clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
                clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
                clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;

                if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
                {
                    // Apply scissor/clipping rectangle
                    commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D( (int32_t)(clip_rect.x), (int32_t)(clip_rect.y) ),
                                                           vk::Extent2D( (uint32_t)(clip_rect.z - clip_rect.x), (uint32_t)(clip_rect.w - clip_rect.y))));

                    // Draw
                    OPTICK_GPU_EVENT("draw_ui_element");
                    commandBuffer.drawIndexed(pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
                }
                
                if(pcmd->TextureId != NULL) {
                    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, {descSet}, {});
                }
                
            }
            idx_offset += pcmd->ElemCount;
        }
        vtx_offset += cmd_list->VtxBuffer.Size;
    }
    
}

void UIRender::initPipeline(vk::RenderPass renderpass) {
    
    // PIPELINE INFO
    
    auto vertShaderCode = Util::readFile("./resources/shaders/uirender.vert.glsl.spv");
    auto fragShaderCode = Util::readFile("./resources/shaders/uirender.frag.glsl.spv");
    
    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    
    moduleInfo.codeSize = vertShaderCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(vertShaderCode.data());
    VkShaderModule vertShaderModule = static_cast<VkShaderModule> (ctx.device->createShaderModule(moduleInfo));
    
    moduleInfo.codeSize = fragShaderCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(fragShaderCode.data());
    VkShaderModule fragShaderModule = static_cast<VkShaderModule> (ctx.device->createShaderModule(moduleInfo));

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    
    // VERTEX INPUT
    
    auto vertexInputBindings = std::vector<vk::VertexInputBindingDescription> {
        vk::VertexInputBindingDescription(0, sizeof(ImDrawVert), vk::VertexInputRate::eVertex),
    };
    // Inpute attribute bindings describe shader attribute locations and memory layouts
    auto vertexInputAttributs = std::vector<vk::VertexInputAttributeDescription> {
        {0, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, pos)},
        {1, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, uv)},
        {2, 0, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col)}
    };

    auto vertexInputState = vk::PipelineVertexInputStateCreateInfo({}, (uint32_t) vertexInputBindings.size(), vertexInputBindings.data(), (uint32_t) vertexInputAttributs.size(), vertexInputAttributs.data());
    
    
    

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) ctx.swap.extent.width;
    viewport.height = (float) ctx.swap.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = ctx.swap.extent;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;
    
    
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f; // Optional
    depthStencil.maxDepthBounds = 1.0f; // Optional
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {}; // Optional
    depthStencil.back = {}; // Optional
    
    
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynInfo = {};
    dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynInfo.dynamicStateCount = 2;
    dynInfo.pDynamicStates = &states[0];
    
    
    
    
    auto layouts = std::vector<vk::DescriptorSetLayout> {descLayout};
    
    auto range = vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, 4 * sizeof(float));
    
    pipelineLayout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo(
        {}, (uint32_t) layouts.size(), layouts.data(), 1, &range
    ));
    
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputState.operator const VkPipelineVertexInputStateCreateInfo &();
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynInfo;
    pipelineInfo.layout = static_cast<VkPipelineLayout>(pipelineLayout);
    pipelineInfo.renderPass = static_cast<VkRenderPass>(renderpass);
    pipelineInfo.subpass = 0;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    graphicsPipeline = ctx.device->createGraphicsPipeline(nullptr, {pipelineInfo}).value;

    ctx.device->destroyShaderModule(static_cast<vk::ShaderModule> (fragShaderModule));
    ctx.device->destroyShaderModule(static_cast<vk::ShaderModule> (vertShaderModule));
    
}

UIRender::~UIRender() {
    
    // Free/Destroy ce qui est créé dans init();
    
    ctx.device->destroy(descPool);
    
    ctx.device->destroy(descLayout);
    
    ctx.device->destroy(fontSampler);
    
    ctx.device->destroy(fontView);
    
    for(FrameDataForRender& fd : g_FramesDataBuffers) {
        ctx.device->destroy(fd.vertexBuffer);
        ctx.device->destroy(fd.indexBuffer);
        ctx.device->free(fd.vertexBufferMemory);
        ctx.device->free(fd.indexBufferMemory);
    }
    
    ctx.device->destroy(graphicsPipeline);
    
    ctx.device->destroy(pipelineLayout);
    
}
