#ifndef DY_PIPELINE_H
#define DY_PIPELINE_H

#include "renderer/util/vk.h"

#include <vector>

namespace dy {

class Device;

// Common state of every graphics pipeline in the engine : one subpass,
// dynamic viewport and scissor, no multisampling.
struct GraphicsPipelineDesc {
    const char* vertex_shader = nullptr;
    const char* fragment_shader = nullptr;
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
};

vk::ShaderModule loadShader(Device& device, const char* path);

vk::Pipeline createGraphicsPipeline(Device& device, const GraphicsPipelineDesc& desc);

}

#endif
