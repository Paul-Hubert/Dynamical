#include "system_list.h"
#include "logic/components/map_configuration.h"

#include <imgui/imgui.h>

using namespace dy;

void MapConfiguratorSys::preinit() {
    auto& conf = reg.set<MapConfiguration>();
    conf.frequency = 1;
    conf.gain = 1;
    conf.lacunarity = 1;
    conf.octave_count = 1;
    conf.seed = 12345;
    conf.weighted_strength = 1;
}

void MapConfiguratorSys::init() {

}

void MapConfiguratorSys::finish() {

}

void MapConfiguratorSys::tick(float dt) {
    auto& conf = reg.ctx<MapConfiguration>();

    static bool open = true;
    if(ImGui::Begin("Map Configurator", &open)) {
        ImGui::InputFloat("Octave count", &conf.octave_count);
        ImGui::InputFloat("Frequency", &conf.frequency);

        //seed is an unsigned int, imgui takes a signed integer but they're the same size
        // and the value doesn't matter so it's okay
        ImGui::InputInt("Seed", (int*)&conf.seed);
        ImGui::InputFloat("Gain", &conf.gain);
        ImGui::InputFloat("Lacunarity", &conf.lacunarity);
    }
    ImGui::End();
}
