#include "pipeline.h"

#include "renderer/context/context.h"
#include "renderer/context/device.h"
#include "util/util.h"

#include <stdexcept>
#include <string>

using namespace dy;

vk::ShaderModule dy::loadShader(Device& device, const char* path) {
    auto code = dy::readFile(path);
    if(code.empty()) {
        throw std::runtime_error(std::string("Missing shader : ") + path);
    }
    return device->createShaderModule(vk::ShaderModuleCreateInfo({}, code.size(), reinterpret_cast<const uint32_t*>(code.data())));
}

vk::Pipeline dy::createGraphicsPipeline(Device& device, const GraphicsPipelineDesc& desc) {

    vk::ShaderModule vert = loadShader(device, desc.vertex_shader.c_str());
    vk::ShaderModule frag = loadShader(device, desc.fragment_shader.c_str());

    auto stages = std::vector {
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, vert, "main", desc.vertex_specialization),
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, frag, "main", desc.fragment_specialization),
    };

    auto vertexInput = vk::PipelineVertexInputStateCreateInfo({}, desc.bindings, desc.attributes);

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);

    // Dynamic, set by the render target
    auto viewportState = vk::PipelineViewportStateCreateInfo({}, 1, nullptr, 1, nullptr);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo({}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill,
        desc.cull, desc.front_face, VK_FALSE, 0.f, 0.f, 0.f, 1.f);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1);

    auto blendAttachment = vk::PipelineColorBlendAttachmentState(
        desc.alpha_blend,
        vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
        vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
        desc.color_write ? vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
            : vk::ColorComponentFlags());
    std::vector<vk::PipelineColorBlendAttachmentState> blendAttachments(desc.color_attachments, blendAttachment);
    auto colorBlending = vk::PipelineColorBlendStateCreateInfo({}, VK_FALSE, vk::LogicOp::eCopy, blendAttachments);

    vk::StencilOpState stencil(vk::StencilOp::eKeep, desc.stencil_pass, vk::StencilOp::eKeep, desc.stencil_compare,
        0xFF, desc.stencil_pass == vk::StencilOp::eKeep ? 0 : 0xFF, desc.stencil_reference);
    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo({}, desc.depth_test, desc.depth_write, vk::CompareOp::eLessOrEqual,
        VK_FALSE, desc.stencil_test, stencil, stencil);

    auto dynamicStates = std::vector { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    auto dynamicState = vk::PipelineDynamicStateCreateInfo({}, dynamicStates);

    auto info = vk::GraphicsPipelineCreateInfo({}, stages, &vertexInput, &inputAssembly, nullptr, &viewportState,
        &rasterizer, &multisampling, &depthStencil, &colorBlending, &dynamicState, desc.layout, desc.renderpass, 0);

    vk::Pipeline pipeline = device->createGraphicsPipeline(nullptr, info).value;

    device->destroy(vert);
    device->destroy(frag);

    return pipeline;

}

std::string dy::sceneFragmentShader(Context& ctx, const char* name) {
    return std::string("./resources/shaders/") + name + (ctx.device.ray.enabled() ? ".frag.glsl.rt.spv" : ".frag.glsl.spv");
}
