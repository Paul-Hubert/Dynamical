#include "system_list.h"

#include <imgui/imgui.h>

#include "logic/components/camera.h"
#include "view/components/input.h"
#include "logic/components/render_settings.h"
#include "logic/components/lighting_stats.h"
#include "logic/components/point_light.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/sky/sky.h"
#include "view/spawn.h"
#include "logic/components/vr.h"
#include "logic/settings/settings.h"
#include "logic/terrain/chunk.h"
#include "logic/animals/animal.h"
#include "logic/animals/gait.h"
#include "logic/animals/herd.h"
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
            ImGui::SameLine();
            ImGui::Checkbox("Soil", &render.soil);
        }

        // The soil map of each chunk : baked again when these change
        if(ImGui::CollapsingHeader("Soil##settings")) {
            auto& soil = render.soil_params;
            ImGui::SliderFloat("Mud cover", &soil.mud.cover, 0.f, 0.5f);
            ImGui::SliderFloat("Mud wavelength", &soil.mud.wavelength, 16.f, 400.f, "%.0f m", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Stones cover", &soil.stones.cover, 0.f, 0.5f);
            ImGui::SliderFloat("Stones wavelength", &soil.stones.wavelength, 16.f, 400.f, "%.0f m", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Sand cover", &soil.sand.cover, 0.f, 0.5f);
            ImGui::SliderFloat("Sand wavelength", &soil.sand.wavelength, 16.f, 400.f, "%.0f m", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderInt("Soil octaves", &soil.octaves, 1, 5);
            ImGui::SliderFloat("Soil gain", &soil.gain, 0.f, 1.f);
            ImGui::SliderFloat("Soil edge width", &soil.edge_width, 4.f, 24.f, "%.0f m");
            ImGui::SliderFloat("Shorter grass from", &soil.grass_margin, 0.f, 24.f, "%.0f m");
            ImGui::SliderFloat("Stones under the rock", &render.scree, 0.f, 1.f);
            ImGui::SliderFloat("Relief of the soil", &render.relief, 0.f, 3.f);
        }

        if(auto* sky = reg.ctx().find<Sky>()) {
            if(ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
                int hours = (int) sky->time, minutes = (int) ((sky->time - (float) hours) * 60.f);
                ImGui::Text("%02d:%02d, the %s is up", hours, minutes, skyLight(*sky).is_sun ? "sun" : "moon");
                ImGui::SliderFloat("Time of the day", &sky->time, 0.f, 24.f, "%.2f h");
                ImGui::SliderFloat("Length of a day", &sky->day_length, 0.f, 3600.f, "%.0f s (0 : stopped)");
                ImGui::SliderFloat("Season", &sky->declination, -23.4f, 23.4f, "%.1f deg");
                size_t torches = reg.view<PointLight>().size();
                if(ImGui::Button("Torch here")) {
                    auto torch = reg.create();
                    reg.emplace<Transform>(torch, camera.position + camera.forward() * 1.5f);
                    reg.emplace<PointLight>(torch);
                }
                ImGui::SameLine();
                if(ImGui::Button("No torches")) {
                    auto view = reg.view<PointLight>();
                    reg.destroy(view.begin(), view.end());
                }
                ImGui::SameLine();
                ImGui::Text("%zu torches", torches);
            }
        }

        // Ray traced lighting : what each target pays for (docs/lighting.md)
        if(auto* lighting = reg.ctx().find<LightingStats>()) {
            if(ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("%u chunks in the rays (%u waiting), %u instances, %.1f MB", lighting->chunks_lit, lighting->chunks_pending,
                    lighting->instances, (double) lighting->structure_bytes / (1024. * 1024.));
                if(lighting->bricks > 0) {
                    ImGui::Text("%u bricks, %u probes traced, %u never traced", lighting->bricks, lighting->probes_active, lighting->probes_pending);
                }
                ImGui::Text("%u plants as leaves in the rays, %u as boxes", lighting->plants_near, lighting->plants_far);
                ImGui::SliderInt("Probe rays a frame", &render.probe_rays_per_frame, 0, 2000000);
                ImGui::SliderInt("Rays a probe", &render.rays_per_probe, 16, 128);
                ImGui::Checkbox("Far plants in the rays", &render.far_plants_in_rays);
                ImGui::SameLine();
                ImGui::Checkbox("Far plants from their volume", &render.far_plants_on_screen);
                const char* const tiers[] = { "off", "low", "medium", "high" };
                const char* const targets[] = { "Desktop", "VR" };
                for(int target = 0; target < 2; target++) {
                    int tier = (int) render.lighting[target].tier;
                    if(ImGui::Combo(targets[target], &tier, tiers, 4)) {
                        render.lighting[target] = RenderSettings::Lighting::of((RenderSettings::LightingTier) tier);
                    }
                }
            }
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

        // Its own id : with the one of the Grass checkbox above, a click never opened it
        if(ImGui::CollapsingHeader("Grass##settings")) {
            auto& grass = render.grass_params;
            ImGui::SliderFloat("Tile size", &grass.tile_size, 0.1f, 4.f);
            ImGui::SliderFloat("Height", &grass.height, 0.01f, 3.f);
            ImGui::SliderInt("Layers", &grass.layers, 1, 3);
            ImGui::SliderFloat("Layer spread", &grass.layer_spread, 0.f, 1.f);
            ImGui::SliderFloat("Undergrowth", &grass.undergrowth, 0.f, 1.f);
            // The height map of each chunk : baked again when these change
            ImGui::SliderFloat("Height map wavelength", &grass.height_map.wavelength, 16.f, 400.f, "%.0f m", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderInt("Height map octaves", &grass.height_map.octaves, 1, 6);
            ImGui::SliderFloat("Height map gain", &grass.height_map.gain, 0.f, 1.f);
            ImGui::SliderFloat("Height map contrast", &grass.height_map.contrast, 0.f, 4.f);
            ImGui::SliderFloat("Height map amount", &grass.patch_amount, 0.f, 0.9f);
            ImGui::SliderFloat("Layer patch size", &grass.patch_size, 4.f, 60.f);
            ImGui::SliderFloat("Layer patches", &grass.layer_patch, 0.f, 1.f);
            ImGui::SliderFloat3("Base normal", &grass.base_normal[0], -1.f, 1.f);
            ImGui::SliderFloat("Follow terrain", &grass.follow_terrain, 0.f, 1.f);
            ImGui::SliderFloat("Wind strength", &grass.wind_strength, 0.f, 0.5f);
            ImGui::SliderFloat("Wind speed", &grass.wind_speed, 0.f, 4.f);
            ImGui::SliderFloat("Curve", &grass.curve, 0.f, 1.f);
            ImGui::SliderFloat("Colour variation", &grass.colour_variation, 0.f, 1.f);
            ImGui::SliderFloat("Dryness", &grass.dryness, 0.f, 1.f);
            ImGui::SliderFloat("Baked colour", &grass.baked_colour, 0.f, 1.f);
            ImGui::SliderFloat("Species patch size", &grass.species.size, 0.f, 120.f, "%.0f m");
            ImGui::SliderFloat("Small plants under", &grass.species.low, 0.f, 1.f);
            ImGui::SliderFloat("Species overlap", &grass.species.overlap, 0.f, 1.f);
            ImGui::SliderFloat("Shadow", &grass.shadow, 0.f, 1.f);
            ImGui::SliderFloat("Canopy", &grass.canopy, 0.f, 1.f);
            ImGui::SliderFloat("See through on crests", &grass.silhouette, 0.f, 3.f);
            ImGui::SliderFloat("Azimuth dither", &grass.azimuth_dither, 0.f, 1.f);
            ImGui::SliderFloat("Top blend start", &grass.top_start, 0.f, 0.95f);
            ImGui::SliderFloat("Noise frequency", &grass.noise_frequency, 0.001f, 1.f, "%.5g", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Noise amplitude", &grass.noise_amplitude, 0.f, 1.f);
        }

        if(auto* vegetation = reg.ctx().find<VegetationStats>()) {
            if(ImGui::CollapsingHeader("Vegetation", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& params = render.vegetation_params;
                ImGui::Text("%u plants, %u drawn, %u triangles", vegetation->plants, vegetation->drawn, vegetation->triangles);
                ImGui::Text("%u species loaded, %u loading", vegetation->species_loaded, vegetation->species_loading);
                ImGui::Checkbox("Vegetation", &render.vegetation);
                ImGui::SameLine();
                ImGui::Checkbox("Normal maps", &params.normal_maps);
                ImGui::SameLine();
                ImGui::Checkbox("Subsurface", &params.subsurface);
                ImGui::SameLine();
                ImGui::Checkbox("Cull", &params.cull);
                ImGui::SliderFloat("Shade under the plants", &render.tree_shade, 0.f, 1.f);
                ImGui::SliderFloat("LOD bias", &params.lod_bias, 0.25f, 16.f, "%.2f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderInt("Force LOD (-1 : by distance)", &params.force_lod, -1, 5);
            }
        }

        if(auto* animals = reg.ctx().find<AnimalStats>()) {
            if(ImGui::CollapsingHeader("Animals", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("%u animals, %u drawn, %u triangles, %u joints", animals->animals, animals->drawn, animals->triangles, animals->joints);
                ImGui::Text("%u species loaded, %u loading", animals->species_loaded, animals->species_loading);
                if(auto* herds = reg.ctx().find<HerdStats>()) {
                    ImGui::Text("%u herds, %u on their way, %u animals walking", herds->herds, herds->travelling, herds->moving);
                }
                ImGui::Checkbox("Animals", &render.animals);
                // The procedural walk : one style for every animal that walks
                if(!reg.ctx().find<GaitStyleOverride>()) reg.ctx().emplace<GaitStyleOverride>();
                auto& gait = reg.ctx().get<GaitStyleOverride>();
                ImGui::SameLine();
                ImGui::Checkbox("One walk for all", &gait.on);
                if(gait.on) {
                    GaitStyle& style = gait.style;
                    ImGui::SliderFloat("Planted feet", &style.ik, 0.f, 1.f);
                    ImGui::SliderFloat("Stride", &style.stride, 0.5f, 1.6f);
                    ImGui::SliderFloat("Step height", &style.step_height, 0.5f, 3.f);
                    ImGui::SliderFloat("Clearance (m)", &style.clearance, 0.f, 0.5f);
                    ImGui::SliderFloat("Soft ground (m)", &style.soft_depth, 0.f, 0.5f);
                    ImGui::SliderFloat("Straight up out of it", &style.exit_vertical, 0.f, 1.f);
                    ImGui::SliderFloat("Straight down into it", &style.entry_vertical, 0.f, 1.f);
                    ImGui::SliderFloat("Body sinks with it", &style.body_sink, 0.f, 1.f);
                    ImGui::SliderFloat("Crouch (m)", &style.crouch, 0.f, 0.4f);
                    ImGui::SliderFloat("Feet apart (m)", &style.spread, -0.1f, 0.3f);
                    ImGui::SliderFloat("Bend with the path", &style.bend, 0.f, 2.f);
                    ImGui::SliderFloat("Head into the turn", &style.head_lead, 0.f, 2.f);
                    ImGui::SliderFloat("Lean into the turn", &style.bank, 0.f, 1.5f);
                }
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
