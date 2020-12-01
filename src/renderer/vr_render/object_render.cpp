#include "object_render.h"

#include "util/util.h"

#include "renderer/context.h"
#include "renderer/vr_render/renderpass.h"
#include "renderer/vr_render/view_ubo.h"

#include "logic/components/model_renderablec.h"
#include "logic/components/transformc.h"
#include "renderer/model/modelc.h"

ObjectRender::ObjectRender(entt::registry& reg, Context& ctx, Renderpass& renderpass, std::vector<vk::DescriptorSetLayout> layouts) : reg(reg), ctx(ctx), renderpass(renderpass),
per_frame(NUM_FRAMES) {

    pool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, NUM_FRAMES, vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, NUM_FRAMES)));

    vk::DescriptorSetLayoutBinding binding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, {});
    set_layout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, binding));

    layouts.push_back(set_layout);

    for (int i = 0; i < per_frame.size(); i++) {

        per_frame[i].set = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &set_layout))[0];

        updateBuffer(i);

    }

    createPipeline(layouts);

}


void ObjectRender::render(vk::CommandBuffer command, uint32_t index) {
    auto& f = per_frame[index];

    command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

    auto view = reg.view<ModelRenderableC>();

    if (view.size() > f.capacity) {
        f.capacity *= 2;
        updateBuffer(index);
    }

    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 1, { f.set }, {});

    int transform_index = 0;

    reg.view<ModelRenderableC, TransformC>().each([&](auto entity, ModelRenderableC& renderable, TransformC& transform) {
        
        OPTICK_GPU_EVENT("draw_model");

        ModelC& model = reg.get<ModelC>(renderable.model);
        if(!model.isReady(reg, entity)) return;

        for(auto& part : model.parts) {

            command.bindIndexBuffer(part.index.buffer->buffer, part.index.offset, vk::IndexType::eUint16);

            command.bindVertexBuffers(0,
                {part.position.buffer->buffer, part.normal.buffer->buffer},
                {part.position.offset, part.normal.offset});

            auto& basis = transform.transform.getBasis();
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    f.pointer[transform_index].mat[i][j] = basis[i][j];
                }
            }

            auto& origin = transform.transform.getOrigin();
            for (int i = 0; i < 3; i++) {
                f.pointer[transform_index].pos[i] = origin[i];
            }

            OPTICK_GPU_EVENT("draw_model_part");
            command.drawIndexed((uint32_t) (part.index.size / sizeof(uint16_t)), 1, 0, 0, transform_index);
            transform_index++;

        }
        
    });

}

ObjectRender::~ObjectRender() {

    ctx.device->destroy(pipeline);

    ctx.device->destroy(layout);

    ctx.device->destroy(set_layout);

    ctx.device->destroy(pool);

}


void ObjectRender::updateBuffer(int i) {
    auto& f = per_frame[i];

    VmaAllocationCreateInfo ainfo{};
    ainfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    ainfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    auto binfo = vk::BufferCreateInfo({}, sizeof(Transform) * f.capacity, vk::BufferUsageFlagBits::eUniformBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i);
    f.buffer = VmaBuffer(ctx.device, &ainfo, binfo);

    VmaAllocationInfo inf;
    vmaGetAllocationInfo(ctx.device, f.buffer.allocation, &inf);
    f.pointer = reinterpret_cast<Transform*>(inf.pMappedData);

    auto bufInfo = vk::DescriptorBufferInfo(f.buffer, 0, sizeof(Transform) * f.capacity);

    ctx.device->updateDescriptorSets({
        vk::WriteDescriptorSet(f.set, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo, nullptr)
        }, {});

}

void ObjectRender::createPipeline(std::vector<vk::DescriptorSetLayout> layouts) {

    // PIPELINE INFO

    auto vertShaderCode = Util::readFile("./resources/shaders/object.vert.glsl.spv");
    auto fragShaderCode = Util::readFile("./resources/shaders/object.frag.glsl.spv");

    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

    moduleInfo.codeSize = vertShaderCode.size() * sizeof(char);
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(vertShaderCode.data());
    VkShaderModule vertShaderModule = static_cast<VkShaderModule> (ctx.device->createShaderModule(moduleInfo));

    moduleInfo.codeSize = fragShaderCode.size() * sizeof(char);
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

    auto vertexInputBindings = std::vector{
        vk::VertexInputBindingDescription(0, 3 * sizeof(float), vk::VertexInputRate::eVertex),
        vk::VertexInputBindingDescription(1, 3 * sizeof(float), vk::VertexInputRate::eVertex)
    };
    // Inpute attribute bindings describe shader attribute locations and memory layouts
    auto vertexInputAttributs = std::vector{
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, 0),
        vk::VertexInputAttributeDescription(1, 1, vk::Format::eR32G32B32Sfloat, 0),
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
    scissor.offset = { 0, 0 };
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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

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
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f; // Optional
    depthStencil.maxDepthBounds = 1.0f; // Optional
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {}; // Optional
    depthStencil.back = {}; // Optional


    VkDynamicState states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynInfo = {};
    dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynInfo.dynamicStateCount = 2;
    dynInfo.pDynamicStates = &states[0];



    {

        layout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo(
            {}, layouts, {}
        ));
    }


    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputState.operator const VkPipelineVertexInputStateCreateInfo & ();
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynInfo;
    pipelineInfo.layout = static_cast<VkPipelineLayout>(layout);
    pipelineInfo.renderPass = static_cast<VkRenderPass>(renderpass);
    pipelineInfo.subpass = 0;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    pipeline = ctx.device->createGraphicsPipeline(nullptr, { pipelineInfo }).value;

    ctx.device->destroyShaderModule(static_cast<vk::ShaderModule> (fragShaderModule));
    ctx.device->destroyShaderModule(static_cast<vk::ShaderModule> (vertShaderModule));

}
