#include "object_render.h"

#include "renderer/context/context.h"

#include "imgui.h"
#include <string.h>

#include "util/util.h"
#include "util/log.h"
#include "logic/map/map_manager.h"

#include "logic/components/camerac.h"
#include "logic/components/renderablec.h"
#include "logic/components/positionc.h"

constexpr int max_objects = 5000;

struct RenderObject {
    glm::vec4 box;
    glm::vec4 color;
};

ObjectRenderSys::ObjectRenderSys(entt::registry& reg) : System(reg) {
    
    Context& ctx = *reg.ctx<Context*>();
    
    per_frame.resize(NUM_FRAMES);
    
    {
        auto poolSizes = std::vector {
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, NUM_FRAMES),
        };
        descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, NUM_FRAMES, (uint32_t) poolSizes.size(), poolSizes.data()));
        
        auto bindings = std::vector {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        };
        descLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, (uint32_t) bindings.size(), bindings.data()));
        
    }
    
    for(int i = 0; i< per_frame.size(); i++) {
        auto& f = per_frame[i];
        
        VmaAllocationCreateInfo info {};
        info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        f.uniformBuffer = VmaBuffer(ctx.device, &info, vk::BufferCreateInfo({}, sizeof(RenderObject) * max_objects, vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive));
        
        VmaAllocationInfo inf;
        vmaGetAllocationInfo(ctx.device, f.uniformBuffer.allocation, &inf);
        
        f.uniformPointer = inf.pMappedData;
        
        f.descSet = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &descLayout))[0];
        
        auto bufferInfo = vk::DescriptorBufferInfo(f.uniformBuffer, 0, f.uniformBuffer.size);
        auto write = vk::WriteDescriptorSet(f.descSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &bufferInfo, nullptr);
        ctx.device->updateDescriptorSets(1, &write, 0, nullptr);
        
    }

    initPipeline(ctx.classic_render.renderpass);
    
}

void ObjectRenderSys::tick(float dt) {
    
    Context& ctx = *reg.ctx<Context*>();
    
    auto& f = per_frame[ctx.frame_index];
    
    auto transfer = ctx.transfer.getCommandBuffer();
    
    auto buffer = reinterpret_cast<RenderObject*> (f.uniformPointer);
    int objectCounter = 0;
    
    auto& map = reg.ctx<MapManager>();
    auto& camera = reg.ctx<CameraC>();
    
    auto corner_pos = map.getChunkPos(camera.corner)-1;
    auto end_pos = map.getChunkPos(camera.corner + camera.size)+1;
    
    for(int x = corner_pos.x; x <= end_pos.x; x++) {
        for(int y = corner_pos.y; y <= end_pos.y; y++) {
            
            auto pos = glm::ivec2(x, y);
            
            Chunk* chunk = map.getChunk(pos);
            if(chunk == nullptr) continue;
            
            for(auto entity : chunk->getObjects()) {
                if(reg.all_of<RenderableC>(entity)) {
                    auto& position = reg.get<PositionC>(entity);
                    auto& renderable = reg.get<RenderableC>(entity);
                    buffer[objectCounter].box.x = position.x;
                    buffer[objectCounter].box.y = position.y;
                    buffer[objectCounter].box.z = renderable.size.x;
                    buffer[objectCounter].box.w = renderable.size.y;
                    buffer[objectCounter].color = renderable.color;
                    
                    objectCounter++;
                    if(objectCounter >= max_objects) {
                        //dy::log(dy::Level::warning) << "too many objects\n";
                        x = end_pos.x + 1;
                        y = end_pos.y + 1;
                        return;
                    }
                }
            }
            
        }
    }
    
    
    /*
    auto view = reg.view<RenderableC, PositionC>();
    for(auto entity : view) {
        auto& position = view.get<PositionC>(entity);
        auto& renderable = view.get<RenderableC>(entity);
        buffer[objectCounter].box.x = position.position.x;
        buffer[objectCounter].box.y = position.position.y;
        buffer[objectCounter].box.z = renderable.size.x;
        buffer[objectCounter].box.w = renderable.size.y;
        buffer[objectCounter].color = renderable.color;
        
        objectCounter++;
    }
    */
    
    ctx.classic_render.command.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
    
    ctx.classic_render.command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, { ctx.classic_render.per_frame[ctx.frame_index].set, f.descSet}, nullptr);
    
    ctx.classic_render.command.draw(6, objectCounter, 0, 0);
    
    
}

void ObjectRenderSys::initPipeline(vk::RenderPass renderpass) {
    
    Context& ctx = *reg.ctx<Context*>();
    
    // PIPELINE INFO
    
    auto vertShaderCode = Util::readFile("./resources/shaders/objectrender.vert.glsl.spv");
    auto fragShaderCode = Util::readFile("./resources/shaders/objectrender.frag.glsl.spv");
    
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
    
    
    
    
    auto layouts = std::vector<vk::DescriptorSetLayout> {ctx.classic_render.view_layout, descLayout};
    
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

ObjectRenderSys::~ObjectRenderSys() {
    
    Context& ctx = *reg.ctx<Context*>();
    
    ctx.device->destroy(descPool);
    
    ctx.device->destroy(descLayout);
    
    ctx.device->destroy(graphicsPipeline);
    
    ctx.device->destroy(pipelineLayout);
    
}



