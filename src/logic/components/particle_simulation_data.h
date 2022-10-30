#ifndef DYNAMICAL_PARTICLE_SIMULATION_DATA_H
#define DYNAMICAL_PARTICLE_SIMULATION_DATA_H

#include "renderer/util/vk.h"

struct ParticleSimulationData {
    vk::DescriptorSetLayout mapLayout;
    vk::DescriptorSet mapSet;
    int num_chunks;
    vk::DescriptorSetLayout objectLayout;
    vk::DescriptorSet objectSet;
    int num_objects;
};


#endif //DYNAMICAL_PARTICLE_SIMULATION_DATA_H
