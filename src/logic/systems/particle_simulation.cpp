#include "particle_simulation.h"

#include "renderer/context/context.h"

#include "util/util.h"
#include "util/log.h"

#include "logic/map/map_manager.h"
#include "logic/components/input.h"

using namespace dy;

ParticleSimulationSys::ParticleSimulationSys(entt::registry& reg) : System(reg) {

    Context& ctx = *reg.ctx<Context*>();

    {

        VmaAllocationCreateInfo info {};
        info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        particleBuffer = VmaBuffer(ctx.device, &info, vk::BufferCreateInfo({}, sizeof(Particle) * max_particles, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer, vk::SharingMode::eExclusive));
        hashmapBuffer = VmaBuffer(ctx.device, &info, vk::BufferCreateInfo({}, sizeof(uint32_t) * 2 * hashmap_slots, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive));

        info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        uniformBuffer = VmaBuffer(ctx.device, &info, vk::BufferCreateInfo({}, sizeof(Particle) * max_new_particles, vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive));


        VmaAllocationInfo inf;
        vmaGetAllocationInfo(ctx.device, uniformBuffer.allocation, &inf);

        uniformPointer = reinterpret_cast<Particle*> (inf.pMappedData);

    }

    {
        auto poolSizes = std::vector {
                vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 3),
                vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1)
        };
        descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, (uint32_t) poolSizes.size(), poolSizes.data()));
    }

    {
        auto bindings = std::vector {
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
                vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute)
        };
        descLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, (uint32_t) bindings.size(), bindings.data()));

        descSet = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &descLayout))[0];

        auto particleInfo = vk::DescriptorBufferInfo(particleBuffer, 0, particleBuffer.size);
        auto hashmapInfo = vk::DescriptorBufferInfo(hashmapBuffer, 0, hashmapBuffer.size);
        auto uniformInfo = vk::DescriptorBufferInfo(uniformBuffer, 0, uniformBuffer.size);
        auto writes = std::vector<vk::WriteDescriptorSet> {
            {descSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &particleInfo, nullptr},
            {descSet, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &hashmapInfo, nullptr},
            {descSet, 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &uniformInfo, nullptr}
        };
        ctx.device->updateDescriptorSets(writes.size(), writes.data(), 0, nullptr);

    }

    initPipeline();

}

void ParticleSimulationSys::tick(double dt) {

    OPTICK_EVENT();

    Context& ctx = *reg.ctx<Context*>();

    AddParticles();

    auto& data = reg.ctx<MapUploadData>();

    auto command = ctx.compute.getCommandBuffer();


    particle_count += new_particle_count;

    if(particle_count > max_particles) {
        particle_count = max_particles;
        new_particle_count = 0;
    }

    PushConstants cons {};
    cons.new_particle_count = new_particle_count;
    cons.particle_count = particle_count;

    new_particle_count = 0;

    data.num_particles = particle_count;
    data.particleBuffer = particleBuffer.buffer;

    command.fillBuffer(hashmapBuffer.buffer, 0, hashmapBuffer.size, 0);

    auto barrier = vk::BufferMemoryBarrier(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderWrite, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, hashmapBuffer.buffer, 0, hashmapBuffer.size);
    command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, {}, {barrier}, {});

    command.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

    command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, { descSet, data.mapSet}, nullptr);

    command.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushConstants), &cons);

    command.dispatch(particle_count, 1, 1);

}

void ParticleSimulationSys::AddParticles() {

    auto& input = reg.ctx<Input>();

    if(input.leftDown) {
        auto &map = reg.ctx<MapManager>();

        glm::vec2 pos = map.getMousePosition();
        Tile *tile = map.getTile(pos);
        if (tile) {

            new_particle_count++;
            uniformPointer[0] = {};
            auto& particle = uniformPointer[0];
            particle.sphere.x = pos.x;
            particle.sphere.y = pos.y;
            particle.sphere.z = tile->level + 0.5f;
            particle.sphere.w = 0.5f;

            particle.color = glm::vec4(0, 0, 1, 1);

        }
    }

}

void ParticleSimulationSys::initPipeline() {

    Context& ctx = *reg.ctx<Context*>();

    // PIPELINE INFO

    auto compShaderCode = dy::readFile("./resources/shaders/particle.comp.glsl.spv");

    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

    moduleInfo.codeSize = compShaderCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(compShaderCode.data());
    VkShaderModule compShaderModule = static_cast<VkShaderModule> (ctx.device->createShaderModule(moduleInfo));

    VkPipelineShaderStageCreateInfo compShaderStageInfo = {};
    compShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compShaderStageInfo.module = compShaderModule;
    compShaderStageInfo.pName = "main";

    auto& data = reg.ctx<MapUploadData>();

    auto layouts = std::vector<vk::DescriptorSetLayout> {descLayout, data.mapLayout};

    auto range = vk::PushConstantRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushConstants));

    pipelineLayout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo(
            {}, (uint32_t) layouts.size(), layouts.data(), 1, &range
    ));

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = static_cast<VkPipelineLayout>(pipelineLayout);
    pipelineInfo.stage = compShaderStageInfo;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    computePipeline = ctx.device->createComputePipeline(nullptr, {pipelineInfo}).value;

    ctx.device->destroyShaderModule(static_cast<vk::ShaderModule> (compShaderModule));

}

ParticleSimulationSys::~ParticleSimulationSys() {

    Context& ctx = *reg.ctx<Context*>();

    ctx.device->destroy(computePipeline);

    ctx.device->destroy(descPool);

    ctx.device->destroy(descLayout);

    ctx.device->destroy(pipelineLayout);

}



