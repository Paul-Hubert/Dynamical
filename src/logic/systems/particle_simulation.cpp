#include "particle_simulation.h"

#include "renderer/context/context.h"

#include "util/util.h"

using namespace dy;

#include "logic/components/map_upload_data.h"

ParticleSimulationSys::ParticleSimulationSys(entt::registry& reg) : System(reg) {

    Context& ctx = *reg.ctx<Context*>();

    initPipeline();

}

void ParticleSimulationSys::tick(float dt) {

    OPTICK_EVENT();

    Context& ctx = *reg.ctx<Context*>();

    auto transfer = ctx.transfer.getCommandBuffer();

    auto& data = reg.ctx<MapUploadData>();

    ctx.classic_render.command.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

    ctx.classic_render.command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, { ctx.classic_render.per_frame[ctx.frame_index].set, data.objectSet}, nullptr);

    ctx.classic_render.command.draw(6, data.num_objects, 0, 0);


}

void ParticleSimulationSys::initPipeline() {

    Context& ctx = *reg.ctx<Context*>();

    auto& data = reg.ctx<MapUploadData>();

    // PIPELINE INFO

    auto compShaderCode = dy::readFile("./resources/shaders/particle.comp.glsl.spv");

    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

    moduleInfo.codeSize = compShaderCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(compShaderCode.data());
    VkShaderModule compShaderModule = static_cast<VkShaderModule> (ctx.device->createShaderModule(moduleInfo));

    VkPipelineShaderStageCreateInfo compShaderStageInfo = {};
    compShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    compShaderStageInfo.module = compShaderModule;
    compShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { compShaderStageInfo };

    auto layouts = std::vector<vk::DescriptorSetLayout> {data.objectLayout};

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

ParticleSimulationSys::~ParticleSimulationSys() {

    Context& ctx = *reg.ctx<Context*>();

    ctx.device->destroy(graphicsPipeline);

    ctx.device->destroy(pipelineLayout);

}



