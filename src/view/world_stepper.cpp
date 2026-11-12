#include "world_stepper.h"

#include <imgui/imgui.h>

#include <algorithm>

#include "scene/world.h"

using namespace dy;

void WorldStepperSys::preinit() {
    reg.ctx().emplace<WorldStepperState>();
}

void WorldStepperSys::tick(double dt) {

    auto& state = reg.ctx().get<WorldStepperState>();
    auto* worlds = reg.ctx().find<Worlds>();
    if(!worlds || worlds->size() == 0) return;

    state.h = (*worlds)[0].reg.ctx().get<WorldClock>().dt;

    int ticks = 0;
    if(!state.paused) {
        accumulator += std::min(dt, 0.25);
        ticks = std::min((int) (accumulator / state.h), max_ticks);
        accumulator -= ticks * state.h;
        // Too slow to keep up: drop the time instead of spiraling
        if(accumulator > state.h) accumulator = 0.;
    } else if(state.step_once) {
        ticks = 1;
        state.step_once = false;
    }

    worlds->step(ticks);
    state.ticks += ticks;
    state.alpha = state.paused ? 1. : accumulator / state.h;

    ImGui::SetNextWindowPos(ImVec2(10, 380), ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Worlds")) {
        ImGui::Text("%zu worlds, tick %llu (h = %.4f s)", worlds->size(), (unsigned long long) state.ticks, state.h);
        ImGui::Checkbox("Pause", &state.paused);
        ImGui::SameLine();
        if(ImGui::Button("Step")) {
            state.paused = true;
            state.step_once = true;
        }
    }
    ImGui::End();

}
