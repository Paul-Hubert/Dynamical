#include "grass_pipeline.h"

#include "util/util.h"

#include <fstream>

#include "renderer/device.h"
#include "renderer/transfer.h"
#include "renderer/swapchain.h"
#include "renderer/main_render/renderpass.h"
#include "renderer/num_frames.h"
#include "renderer/main_render/ubo_descriptor.h"

#include "renderer/marching_cubes/terrain.h"

#include "raycast/include/raycast_data.h"

#include "cereal/archives/portable_binary.hpp"
#include "imgui.h"

#include "util/tiled_noise.h"

constexpr int noiseSize = 512;

GrassPipeline::GrassPipeline(Device& device, Transfer& transfer, Swapchain& swap, Renderpass& renderpass, UBODescriptor& ubo) : device(device), transfer(transfer), swap(swap), renderpass(renderpass) {
    
    pc.tile_size = 1;
    pc.grass_height = 1;
    pc.base_normal[0] = 0;
    pc.base_normal[1] = 1;
    pc.base_normal[2] = 0;
    pc.base_normal[3] = 0;
    pc.noise_frequency = 0.0001;
    pc.noise_amplitude = 0.1;
    
    {
        auto poolSizes = std::vector {
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2),
        };
        descPool = device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, poolSizes.size(), poolSizes.data()));
    }
    
    {
        auto bindings = std::vector {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
        };
        descLayout = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings.size(), bindings.data()));
        
        descSet = device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &descLayout))[0];
        
        
        bool concurrent = (device.g_i != device.t_i);
        uint32_t qfs[2] = {device.g_i, device.t_i};
        
        /*
        sampler = device->createSampler(vk::SamplerCreateInfo(
            {}, vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest,
            vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat
        ));
        */
        
        
        sampler = device->createSampler(vk::SamplerCreateInfo(
            {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat
            //, 0, VK_TRUE, device.properties.limits.maxSamplerAnisotropy
        ));
        
        
        {

            VmaAllocationCreateInfo info {};
            info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            raycastImage = VmaImage(device, &info, vk::ImageCreateInfo(
                {}, vk::ImageType::e3D, vk::Format::eR8G8B8A8Unorm, vk::Extent3D(num_samples, num_samples, num_angles), 1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, 
                concurrent ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive, concurrent ? 2 : 1, &qfs[0], vk::ImageLayout::eUndefined)
            );
            //SET_NAME(vk::ObjectType::eImage, (VkImage) raycastImage, Raycast3DTexture)
            
            raycastImageView = device->createImageView(vk::ImageViewCreateInfo({}, raycastImage.image, vk::ImageViewType::e3D, vk::Format::eR8G8B8A8Unorm, vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
            
        }

        std::ifstream is("./resources/raycast_data.bin", std::ios::binary);
        cereal::PortableBinaryInputArchive in(is);
        RaycastData* raycast = new RaycastData();
        in(*raycast);
        
        transfer.prepareImage(raycast->data.data(), sizeof(uint8_t) * 4 * num_samples * num_samples * num_angles, raycastImage, vk::Extent3D(num_samples, num_samples, num_angles), 0, 0);
        
        delete raycast;
        
        

        {

            auto imageInfo = vk::ImageCreateInfo(
                {}, vk::ImageType::e2D, vk::Format::eR8G8B8Snorm, vk::Extent3D(noiseSize, noiseSize, 1), 1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                concurrent ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive, concurrent ? 2 : 1, &qfs[0], vk::ImageLayout::eUndefined);

            vk::ImageFormatProperties props;
            vk::Result res = device.physical.getImageFormatProperties(imageInfo.format, imageInfo.imageType, imageInfo.tiling, imageInfo.usage, imageInfo.flags, &props);
            if(res == vk::Result::eErrorFormatNotSupported) {
                imageInfo.format = vk::Format::eR8G8B8A8Snorm;
            }

            VmaAllocationCreateInfo info {};
            info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            noiseImage = VmaImage(device, &info, imageInfo);
            //SET_NAME(vk::ObjectType::eImage, (VkImage) raycastImage, Raycast3DTexture)
            
            noiseImageView = device->createImageView(vk::ImageViewCreateInfo({}, noiseImage, vk::ImageViewType::e2D, vk::Format::eR8G8B8Snorm, vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
            
        }
        
        Util::TiledNoise* noise = new Util::TiledNoise();
        
        int8_t* noiseData = new int8_t[3*noiseSize*noiseSize];
        
        for(int x = 0; x<noiseSize; x++) {
            for(int y = 0; y<noiseSize; y++) {
                noiseData[(x*noiseSize+y) * 3] = static_cast<int8_t> (std::clamp(noise->octaveNoise(x*10./noiseSize, y*10./noiseSize, 0, 2) * 128., -127., 127.));
                noiseData[(x*noiseSize+y) * 3 + 1] = static_cast<int8_t> (std::clamp(noise->octaveNoise(x*10./noiseSize, y*10./noiseSize, 1000, 2) * 128., -127., 127.));
                noiseData[(x*noiseSize+y) * 3 + 2] = static_cast<int8_t> (std::clamp(noise->octaveNoise(x*10./noiseSize, y*10./noiseSize, 2000, 2) * 128., -127., 127.));
            }
        }
        
        transfer.prepareImage(noiseData, 3 * sizeof(int8_t) * noiseSize * noiseSize, noiseImage, vk::Extent3D(noiseSize, noiseSize, 1), 0, 0);
        
        delete[] noiseData;
        
        const auto raycast_info = vk::DescriptorImageInfo(sampler, raycastImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
        const auto noise_info = vk::DescriptorImageInfo(sampler, noiseImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
        device->updateDescriptorSets({
            vk::WriteDescriptorSet(descSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &raycast_info, nullptr, nullptr),
            vk::WriteDescriptorSet(descSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &noise_info, nullptr, nullptr)
        }, {});
        
    }
    
    /*
    struct Constants {
        int grass_height;
        int tile_size;
    } constants;
    constants.grass_height = grass_height;
    constants.tile_size = tile_size;
    
    auto mapEntries = std::array {
        vk::SpecializationMapEntry(0, offsetof(Constants, grass_height), sizeof(int)),
        vk::SpecializationMapEntry(1, offsetof(Constants, tile_size), sizeof(int))
    };
    
    auto specConstants = vk::SpecializationInfo(mapEntries.size(), mapEntries.data(), sizeof(Constants), &constants);
    */
    
    // PIPELINE INFO
    
    auto vertShaderCode = Util::readFile("./resources/shaders/grass.vert.glsl.spv");
    auto fragShaderCode = Util::readFile("./resources/shaders/grass.frag.glsl.spv");
    
    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    
    moduleInfo.codeSize = vertShaderCode.size() * sizeof(char);
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(vertShaderCode.data());
    VkShaderModule vertShaderModule = static_cast<VkShaderModule> (device->createShaderModule(moduleInfo));
    
    moduleInfo.codeSize = fragShaderCode.size() * sizeof(char);
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(fragShaderCode.data());
    VkShaderModule fragShaderModule = static_cast<VkShaderModule> (device->createShaderModule(moduleInfo));

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";
    //vertShaderStageInfo.pSpecializationInfo = reinterpret_cast<VkSpecializationInfo*> (&specConstants);

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";
    //fragShaderStageInfo.pSpecializationInfo = reinterpret_cast<VkSpecializationInfo*> (&specConstants);

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    
    // VERTEX INPUT
    
    auto vertexInputBindings = std::vector {
        vk::VertexInputBindingDescription(0, sizeof(Vertex), vk::VertexInputRate::eVertex),
    };
    // Inpute attribute bindings describe shader attribute locations and memory layouts
    auto vertexInputAttributs = std::vector {
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)),
        vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv)),
    };
    
    auto vertexInputState = vk::PipelineVertexInputStateCreateInfo({}, vertexInputBindings.size(), vertexInputBindings.data(), vertexInputAttributs.size(), vertexInputAttributs.data());
    
    
    
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = swap.extent.height;
    viewport.width = (float) swap.extent.width;
    viewport.height = - ((float)swap.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = swap.extent;

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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp=VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp=VK_BLEND_OP_ADD;

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
    
    
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynInfo = {};
    dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynInfo.dynamicStateCount = 2;
    dynInfo.pDynamicStates = &states[0];
    
    
    
    {
        auto layouts = std::vector<vk::DescriptorSetLayout> {ubo.descLayout, descLayout};
        
        auto pc = vk::PushConstantRange(vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex, 0, sizeof(GrassPC));
        
        layout = device->createPipelineLayout(vk::PipelineLayoutCreateInfo(
            {}, layouts.size(), layouts.data(), 1, &pc
        ));
    }
    
    
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
    pipelineInfo.layout = static_cast<VkPipelineLayout>(layout);
    pipelineInfo.renderPass = static_cast<VkRenderPass>(renderpass);
    pipelineInfo.subpass = 0;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    pipeline = device->createGraphicsPipelines(nullptr, {pipelineInfo})[0];

    device->destroyShaderModule(static_cast<vk::ShaderModule> (fragShaderModule));
    device->destroyShaderModule(static_cast<vk::ShaderModule> (vertShaderModule));
    
}

void GrassPipeline::makeDebugWindow() {
    
    ImGui::Begin("Grass Debug Window");
    
    ImGui::SliderFloat("tile_size", &pc.tile_size, 0.01, 1);
    
    ImGui::SliderFloat("grass_height", &pc.grass_height, 0.01, 5);
    
    ImGui::SliderFloat3("base_normal", &pc.base_normal[0], -1, 1);
    
    ImGui::SliderFloat("noise_frequency", &pc.noise_frequency, 0.0001, 1, "%.5g", 10);
    
    ImGui::SliderFloat("noise_amplitude", &pc.noise_amplitude, 0.0001, 1, "%.5g", 10);
    
    ImGui::End();
    
}


GrassPipeline::~GrassPipeline() {
    
    device->destroy(pipeline);
    
    device->destroy(layout);
    
    
    device->destroy(noiseImageView);
    
    device->destroy(raycastImageView);
    
    
    device->destroy(sampler);
    
    
    device->destroy(descLayout);
    
    
    device->destroy(descPool);
    
    
    
    
}
