#include "map_render.h"

#include "renderer/context/context.h"

#include "imgui.h"
#include <string.h>

#include "util/util.h"
#include "util/log.h"

#include "logic/map/tile.h"
#include "logic/map/chunk.h"
#include "logic/map/map_manager.h"

#include "logic/components/camera.h"

using namespace dy;

constexpr int max_chunks = 5000;
constexpr int max_stored_chunks = 20000;

struct Header {
    glm::vec4 colors[Tile::Type::max];
    glm::ivec2 corner_indices;
    int chunk_length;
    int chunk_indices[max_chunks];
};

struct RenderChunk {
    int tiles[Chunk::size*Chunk::size];
};

MapRenderSys::MapRenderSys(entt::registry& reg) : System(reg) {
    
    Context& ctx = *reg.ctx<Context*>();
    
    per_frame.resize(NUM_FRAMES);
    
    stored_chunks.resize(max_stored_chunks);
    
    for(int i = 0; i< per_frame.size(); i++) {
        auto& f = per_frame[i];
        
        VmaAllocationCreateInfo info {};
        info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        f.stagingBuffer = VmaBuffer(ctx.device, &info, vk::BufferCreateInfo({}, sizeof(Header) + max_chunks * sizeof(RenderChunk), vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive));
        
        VmaAllocationInfo inf;
        vmaGetAllocationInfo(ctx.device, f.stagingBuffer.allocation, &inf);
        
        f.stagingPointer = inf.pMappedData;
    }
    
    {
        
        VmaAllocationCreateInfo info {};
        info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        storageBuffer = VmaBuffer(ctx.device, &info, vk::BufferCreateInfo({}, sizeof(Header) + max_stored_chunks * sizeof(RenderChunk), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive));
        
    }
    
    {
        auto poolSizes = std::vector {
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1),
        };
        descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, (uint32_t) poolSizes.size(), poolSizes.data()));
    }
    
    {
        auto bindings = std::vector {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment)
        };
        descLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, (uint32_t) bindings.size(), bindings.data()));
        
        descSet = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &descLayout))[0];
        
        auto info = vk::DescriptorBufferInfo(storageBuffer, 0, storageBuffer.size);
        auto write = vk::WriteDescriptorSet(descSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &info, nullptr);
        ctx.device->updateDescriptorSets(1, &write, 0, nullptr);
        
    }

    initPipeline(ctx.classic_render.renderpass);
    
}

void MapRenderSys::tick(float dt) {
    
    Context& ctx = *reg.ctx<Context*>();
    
    auto& f = per_frame[ctx.frame_index];
    
    auto transfer = ctx.transfer.getCommandBuffer();
    
    std::vector<vk::BufferCopy> regions;
    
    auto header = reinterpret_cast<Header*> (f.stagingPointer);
    
    memset(header, 0, sizeof(Header));
    
    header->colors[Tile::nothing] = glm::vec4(0,0,0,0);
    header->colors[Tile::dirt] = glm::vec4(0.6078,0.4627,0.3255,1);
    header->colors[Tile::grass] = glm::vec4(0.3373,0.4902,0.2745,1);
    header->colors[Tile::stone] = glm::vec4(0.6118,0.6353,0.7216,1);
    header->colors[Tile::sand] = glm::vec4(0.761,0.698,0.502,1);
    header->colors[Tile::water] = glm::vec4(0.451,0.7137,0.9961,1);
    
    regions.push_back(vk::BufferCopy(0, 0, sizeof(Header)));
    
    int staging_counter = 0;
    
    
    auto& map = reg.ctx<MapManager>();
    auto& camera = reg.ctx<Camera>();
    
    auto corner_pos = map.getChunkPos(camera.corner)-1;
    auto end_pos = map.getChunkPos(camera.corner + camera.size)+1;
    
    header->corner_indices = corner_pos;
    header->chunk_length = end_pos.y - corner_pos.y + 1;
    int chunk_indices_counter = 0;
    
    for(int x = corner_pos.x; x <= end_pos.x; x++) {
        for(int y = corner_pos.y; y <= end_pos.y; y++) {
            
            auto pos = glm::ivec2(x, y);
            
            bool stored = false;
            for(int i = 0; i<stored_chunks.size(); i++) {
                if(stored_chunks[i].stored && stored_chunks[i].position == pos) {
                    stored = true;
                    header->chunk_indices[chunk_indices_counter] = i;
                    chunk_indices_counter++;
                    stored_chunks[i].used = true;
                    break;
                }
            }
            
            
            
            if(!stored) {
                
                int index;
                
                if(stored_chunks.size() >= max_stored_chunks) {
                    auto& sc = stored_chunks[storage_counter];
                    while(
                        sc.stored
                        && (sc.used
                        || (sc.position.x >= corner_pos.x && sc.position.x <= end_pos.x
                        && sc.position.y >= corner_pos.y && sc.position.y <= end_pos.y))
                    ) {
                        storage_counter = (storage_counter+1)%max_stored_chunks;
                        sc = stored_chunks[storage_counter];
                    }
                    index = storage_counter;
                    storage_counter = (storage_counter+1)%max_stored_chunks;
                } else {
                    index = stored_chunks.size();
                    stored_chunks.push_back(StoredChunk{pos});
                    storage_counter = (storage_counter+1)%max_stored_chunks;
                }
                
                Chunk* chunk = map.getChunk(pos);
                if(chunk == nullptr) chunk = map.generateChunk(pos);
                
                RenderChunk* rchunk = reinterpret_cast<RenderChunk*>(header+1);
                
                for(int i = 0; i<Chunk::size; i++) {
                    for(int j = 0; j<Chunk::size; j++) {
                        rchunk[staging_counter].tiles[i * Chunk::size + j] = chunk->get(glm::vec2(i,j)).terrain;
                    }
                }
                
                regions.push_back(vk::BufferCopy(sizeof(Header) + staging_counter * sizeof(RenderChunk), sizeof(Header) + index * sizeof(RenderChunk), sizeof(RenderChunk)));
                
                header->chunk_indices[chunk_indices_counter] = index;
                chunk_indices_counter++;
                
                
                stored_chunks[index].position = pos;
                stored_chunks[index].used = true;
                stored_chunks[index].stored = true;
                
                staging_counter++;
                
            }
            
            if(chunk_indices_counter >= max_chunks) {
                dy::log(dy::Level::warning) << "too many chunks\n";
                x = end_pos.x + 1;
                y = end_pos.y + 1;
                break;
            }
            
        }
    }
    
    for(int i = 0; i<stored_chunks.size(); i++) {
        stored_chunks[i].used = false;
    }
    
    transfer.copyBuffer(f.stagingBuffer, storageBuffer, regions);
    
    
    ctx.classic_render.command.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
    
    ctx.classic_render.command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, { ctx.classic_render.per_frame[ctx.frame_index].set, descSet}, nullptr);
    
    ctx.classic_render.command.draw(3, 1, 0, 0);
    
    
}

void MapRenderSys::initPipeline(vk::RenderPass renderpass) {
    
    Context& ctx = *reg.ctx<Context*>();
    
    // PIPELINE INFO
    
    auto vertShaderCode = dy::readFile("./resources/shaders/maprender.vert.glsl.spv");
    auto fragShaderCode = dy::readFile("./resources/shaders/maprender.frag.glsl.spv");
    
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
    
    auto specEntries = std::vector {
        vk::SpecializationMapEntry(0, 0, sizeof(int)),
        vk::SpecializationMapEntry(1, sizeof(int), sizeof(int)),
        vk::SpecializationMapEntry(2, 2 * sizeof(int), sizeof(int))
    };
    
    auto specValues = std::vector<int> {Chunk::size, Tile::Type::max, max_chunks};
    auto specInfo = vk::SpecializationInfo(specEntries.size(), specEntries.data(), specValues.size() * sizeof(int), specValues.data());

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

MapRenderSys::~MapRenderSys() {
    
    Context& ctx = *reg.ctx<Context*>();
    
    ctx.device->destroy(descPool);
    
    ctx.device->destroy(descLayout);
    
    ctx.device->destroy(graphicsPipeline);
    
    ctx.device->destroy(pipelineLayout);
    
}


