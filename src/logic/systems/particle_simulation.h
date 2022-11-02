#ifndef DYNAMICAL_PARTICLE_SIMULATION_H
#define DYNAMICAL_PARTICLE_SIMULATION_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include <imgui/imgui.h>

#include "system.h"

#include <entt/entt.hpp>

#include "logic/components/map_upload_data.h"

#include <glm/glm.hpp>

#include <memory>

namespace dy {

class Context;

const uint32_t max_particles = 100000;
const uint32_t hashmap_slots = 131072; //next pow^2
const uint32_t max_new_particles = 100;

struct PushConstants {
    uint32_t particle_count;
    uint32_t new_particle_count;
};

class ParticleSimulationSys : public System {
public:
    ParticleSimulationSys(entt::registry& reg);
    ~ParticleSimulationSys() override;

    void AddParticles();

    const char* name() override {
        return "ParticleSimulation";
    }

    void tick(double dt) override;

private:

    uint32_t new_particle_count = 0;
    uint32_t particle_count = 0;

    void initPipeline();

    VmaBuffer particleBuffer;
    VmaBuffer hashmapBuffer;
    VmaBuffer uniformBuffer;
    Particle* uniformPointer;

    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;
    vk::DescriptorSet descSet;

    vk::PipelineLayout pipelineLayout;
    vk::Pipeline computePipeline;

};

}

#endif //DYNAMICAL_PARTICLE_SIMULATION_H

