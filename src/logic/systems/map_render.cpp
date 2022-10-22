#include "map_render.h"

#include "renderer/context/context.h"

#include <imgui/imgui.h>
#include <string.h>

#include "util/util.h"
#include "util/log.h"
#include "util/color.h"

#include "logic/map/tile.h"
#include "logic/map/chunk.h"
#include "logic/map/map_manager.h"

#include "map_upload.h"

#include "logic/components/camera.h"

using namespace dy;

MapRenderSys::MapRenderSys(entt::registry& reg) : System(reg) {

    Context& ctx = *reg.ctx<Context*>();

    initPipeline();

    std::vector<int> indices;

    float gridSize = Chunk::size + 1;

    for(int x = 0; x<Chunk::size; x++) {
        for(int y = 0; y<Chunk::size; y++) {

            int vertex_index = x + y * gridSize;
            indices.push_back(vertex_index);
            indices.push_back(vertex_index + 1);
            indices.push_back(vertex_index + gridSize);
            indices.push_back(vertex_index + 1);
            indices.push_back(vertex_index + gridSize + 1);
            indices.push_back(vertex_index + gridSize);

        }
    }

    numIndices = indices.size();

    indexBuffer = ctx.transfer.createBuffer(indices.data(), vk::BufferCreateInfo({}, indices.size() * sizeof(uint32_t), vk::BufferUsageFlagBits::eIndexBuffer, vk::SharingMode::eExclusive));

}

void MapRenderSys::tick(float dt) {
    
    OPTICK_EVENT();

    Context& ctx = *reg.ctx<Context*>();

    MapUploadData& data = reg.ctx<MapUploadData>();
    
    ctx.classic_render.command.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
    
    ctx.classic_render.command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, { ctx.classic_render.per_frame[ctx.frame_index].set, data.descSet}, nullptr);

    ctx.classic_render.command.bindIndexBuffer(indexBuffer->buffer, 0, vk::IndexType::eUint32);

    ctx.classic_render.command.drawIndexed(numIndices, data.num_chunks, 0, 0, 0);

}

void MapRenderSys::initPipeline() {

    Context& ctx = *reg.ctx<Context*>();

    MapUploadData& data = reg.ctx<MapUploadData>();
    
    // PIPELINE INFO
    
    auto vertShaderCode = dy::readFile("resources/shaders/maprender.vert.glsl.spv");
    auto fragShaderCode = dy::readFile("resources/shaders/maprender.frag.glsl.spv");
    
    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    
    moduleInfo.codeSize = vertShaderCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(vertShaderCode.data());
    VkShaderModule vertShaderModule = static_cast<VkShaderModule> (ctx.device->createShaderModule(moduleInfo));
    
    moduleInfo.codeSize = fragShaderCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(fragShaderCode.data());
    VkShaderModule fragShaderModule = static_cast<VkShaderModule> (ctx.device->createShaderModule(moduleInfo));

    auto specEntries = std::vector {
        vk::SpecializationMapEntry(0, 0, sizeof(int)),
        vk::SpecializationMapEntry(1, sizeof(int), sizeof(int)),
        vk::SpecializationMapEntry(2, 2 * sizeof(int), sizeof(int))
    };

    auto specValues = std::vector<int> {Chunk::size, Tile::Type::max, max_chunks};
    auto specInfo = vk::SpecializationInfo(specEntries.size(), specEntries.data(), specValues.size() * sizeof(int), specValues.data());

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";
    vertShaderStageInfo.pSpecializationInfo = reinterpret_cast<VkSpecializationInfo*> (&specInfo);

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";
    fragShaderStageInfo.pSpecializationInfo = reinterpret_cast<VkSpecializationInfo*> (&specInfo);

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    
    // VERTEX INPUT
    
    auto vertexInputBindings = std::vector<vk::VertexInputBindingDescription> {};
    // Inpute attribute bindings describe shader attribute locations and memory layouts
    auto vertexInputAttributs = std::vector<vk::VertexInputAttributeDescription> {};

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
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
    
    
    
    
    auto layouts = std::vector<vk::DescriptorSetLayout> {ctx.classic_render.view_layout, data.descLayout};
    
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
    pipelineInfo.renderPass = static_cast<VkRenderPass>(ctx.classic_render.renderpass);
    pipelineInfo.subpass = 0;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    graphicsPipeline = ctx.device->createGraphicsPipeline(nullptr, {pipelineInfo}).value;

    ctx.device->destroyShaderModule(static_cast<vk::ShaderModule> (fragShaderModule));
    ctx.device->destroyShaderModule(static_cast<vk::ShaderModule> (vertShaderModule));
    
}

MapRenderSys::~MapRenderSys() {

    Context& ctx = *reg.ctx<Context*>();
    
    ctx.device->destroy(graphicsPipeline);
    
    ctx.device->destroy(pipelineLayout);
    
}


