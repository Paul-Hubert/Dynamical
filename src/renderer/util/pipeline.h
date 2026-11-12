#ifndef DY_PIPELINE_H
#define DY_PIPELINE_H

#include "renderer/util/vk.h"

#include <string>
#include <vector>

namespace dy {

class Device;
class Context;

// Common state of every graphics pipeline in the engine : one subpass,
// dynamic viewport and scissor, no multisampling.
struct GraphicsPipelineDesc {
    std::string vertex_shader;
    std::string fragment_shader;
    const vk::SpecializationInfo* vertex_specialization = nullptr;
    const vk::SpecializationInfo* fragment_specialization = nullptr;

    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;

    vk::PipelineLayout layout;
    vk::RenderPass renderpass;

    vk::CullModeFlags cull = vk::CullModeFlagBits::eBack;
    vk::FrontFace front_face = vk::FrontFace::eCounterClockwise;
    bool depth_test = true;
    bool depth_write = true;
    bool alpha_blend = false;
    uint32_t color_attachments = 1;     // of the subpass, all blended the same way
    bool color_write = true;            // false : the depth and the stencil only

    // The stencil of the scene passes (where Context::sceneStencil()) : the test against the reference, and
    // what a fragment that passes it and the depth test leaves there
    bool stencil_test = false;
    vk::CompareOp stencil_compare = vk::CompareOp::eAlways;
    vk::StencilOp stencil_pass = vk::StencilOp::eKeep;
    uint32_t stencil_reference = 0;
};

vk::ShaderModule loadShader(Device& device, const char* path);

// The compiled fragment shader of a scene pass : resources/shaders/<name>.frag.glsl.spv, or its variant with
// ray queries where the device has them (docs/lighting.md)
std::string sceneFragmentShader(Context& ctx, const char* name);

vk::Pipeline createGraphicsPipeline(Device& device, const GraphicsPipelineDesc& desc);

}

#endif
