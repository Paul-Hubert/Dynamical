#include "system_list.h"

#include <imgui/imgui.h>

#include "logic/components/camera.h"
#include "view/components/input.h"
#include "logic/components/render_settings.h"
#include "logic/components/rigid_body.h"
#include "view/spawn.h"
#include "logic/components/vr.h"
#include "logic/settings/settings.h"
#include "logic/terrain/chunk.h"
#include "logic/vegetation/vegetation.h"

#include "renderer/terrain/terrain_sys.h"

using namespace dy;

void DebugMenuSys::preinit() {

}

void DebugMenuSys::init() {

}

void DebugMenuSys::tick(double dt) {

    OPTICK_EVENT();

    auto& settings = reg.ctx().get<Settings>();
    auto& render = reg.ctx().get<RenderSettings>();
    auto& camera = reg.ctx().get<Camera>();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Dynamical")) {

        ImGui::Text("%.2f ms, %.0f fps", dt * 1000., 1. / dt);
        ImGui::Text("Camera %.1f %.1f %.1f", camera.position.x, camera.position.y, camera.position.z);
        ImGui::TextDisabled("WASD + Space/Ctrl to fly, right mouse to look, wheel for speed, Shift to sprint");

        if(ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen)) {
            size_t total = reg.view<ChunkC>().size();
            size_t generated = reg.view<ChunkGenerated>().size();
            size_t meshed = reg.view<ChunkReady>().size();
            ImGui::Text("Chunks : %zu loaded, %zu generated, %zu meshed", total, generated, meshed);
            ImGui::SliderInt("View distance", &settings.view_distance, 256, 2048);
            ImGui::Checkbox("Terrain", &render.terrain);
            ImGui::SameLine();
            ImGui::Checkbox("Grass", &render.grass);
            ImGui::SameLine();
            ImGui::Checkbox("Models", &render.models);
        }

        if(ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
            if(auto* stats = reg.ctx().find<PhysicsStats>()) {
                ImGui::Text("Bodies : %u (%u active)", stats->bodies, stats->active_bodies);
                ImGui::Text("Terrain colliders : %u cells, %u triangles", stats->terrain_cells, stats->terrain_triangles);
                ImGui::Text("Step %.2f ms x%d, colliders %.2f ms", stats->step_ms, stats->steps_last_frame, stats->colliders_ms);
            }
            ImGui::TextDisabled("Left click throws a box");
            if(ImGui::Button("Drop 50 boxes")) {
                for(int i = 0; i < 50; i++) {
                    glm::vec3 offset((i % 5) * 1.5f - 3.f, 10.f + (i / 25) * 1.5f, ((i / 5) % 5) * 1.5f - 3.f);
                    spawnBox(reg, camera.position + camera.forward() * 15.f + offset, 1.f, glm::vec4(0.9f, 0.5f + 0.01f * i, 0.2f, 1.f));
                }
            }
        }

        if(ImGui::CollapsingHeader("Grass")) {
            auto& grass = render.grass_params;
            ImGui::SliderFloat("Tile size", &grass.tile_size, 0.01f, 1.f);
            ImGui::SliderFloat("Height", &grass.height, 0.01f, 5.f);
            ImGui::SliderFloat3("Base normal", &grass.base_normal[0], -1.f, 1.f);
            ImGui::SliderFloat("Noise frequency", &grass.noise_frequency, 0.0001f, 1.f, "%.5g", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Noise amplitude", &grass.noise_amplitude, 0.0001f, 1.f, "%.5g", ImGuiSliderFlags_Logarithmic);
        }

        if(auto* vegetation = reg.ctx().find<VegetationStats>()) {
            if(ImGui::CollapsingHeader("Vegetation", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& params = render.vegetation_params;
                ImGui::Text("%u plants, %u drawn, %u triangles", vegetation->plants, vegetation->drawn, vegetation->triangles);
                ImGui::Text("%u species loaded", vegetation->species_loaded);
                ImGui::Checkbox("Vegetation", &render.vegetation);
                ImGui::SameLine();
                ImGui::Checkbox("Normal maps", &params.normal_maps);
                ImGui::SameLine();
                ImGui::Checkbox("Subsurface", &params.subsurface);
                ImGui::SliderFloat("LOD bias", &params.lod_bias, 0.25f, 16.f, "%.2f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderInt("Force LOD (-1 : by distance)", &params.force_lod, -1, 5);
            }
        }

        if(ImGui::CollapsingHeader("VR", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto* vr = reg.ctx().find<VRInput>();
            if(!vr || !vr->enabled) {
                ImGui::Text("No OpenXR headset (vr_mode=%d)", settings.vr_mode);
            } else {
                ImGui::Text("Session %s, %s", vr->session_running ? "running" : "idle", vr->focused ? "focused" : "not focused");
                auto& rig = reg.ctx().get<VRRig>();
                ImGui::Text("Play area %.1f %.1f %.1f", rig.position.x, rig.position.y, rig.position.z);
                if(ImGui::Button("Move play area to camera (R)")) {
                    reg.ctx().get<Input>().on.set(Input::VR_RIG);
                }
            }
        }

    }
    ImGui::End();

}

void DebugMenuSys::finish() {

}
