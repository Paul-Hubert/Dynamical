#ifndef DYNAMICAL_PARTICLE_SIMULATION_H
#define DYNAMICAL_PARTICLE_SIMULATION_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include <imgui/imgui.h>

#include "system.h"

#include <entt/entt.hpp>

#include <glm/glm.hpp>

#include <memory>

namespace dy {

    class Context;
    class Renderpass;

    class ParticleSimulationSys : public System {
    public:
        ParticleSimulationSys(entt::registry& reg);
        ~ParticleSimulationSys() override;

        const char* name() override {
            return "ParticleSimulation";
        }

        void tick(float dt) override;

    private:

        void initPipeline();

        vk::PipelineLayout pipelineLayout;
        vk::Pipeline graphicsPipeline;

    };

}

#endif //DYNAMICAL_PARTICLE_SIMULATION_H

