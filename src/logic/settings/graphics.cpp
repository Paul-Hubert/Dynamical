#include "graphics.h"

#include "util/log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

using namespace dy;

namespace {

using json = nlohmann::json;

// The group of a level, or nothing : every value has a default
json group(const json& in, const char* name) {
    auto it = in.find(name);
    return it != in.end() && it->is_object() ? *it : json::object();
}

// The values of a target. Its tier is only a name in the debug menu : the highest one that traces no further
RenderSettings::Lighting readLighting(const json& in, RenderSettings::Lighting lighting) {
    lighting.traced_range = in.value("traced_range", lighting.traced_range);
    lighting.sun_rays = in.value("sun_rays", lighting.sun_rays);
    lighting.plant_geometry_range = in.value("plant_geometry_range", lighting.plant_geometry_range);
    lighting.shadowed_torches = in.value("shadowed_torches", lighting.shadowed_torches);
    lighting.animals = in.value("animals", lighting.animals);

    using Tier = RenderSettings::LightingTier;
    lighting.tier = Tier::Off;
    if(lighting.sun_rays > 0 && lighting.traced_range > 0.f) {
        lighting.tier = Tier::Low;
        for(Tier tier : { Tier::Medium, Tier::High }) {
            if(lighting.traced_range >= RenderSettings::Lighting::of(tier).traced_range) lighting.tier = tier;
        }
    }
    return lighting;
}

GraphicsPreset readPreset(const json& in) {

    GraphicsPreset p;
    p.name = in.value("name", std::string());
    p.view_distance = std::clamp(in.value("view_distance", p.view_distance), 64, 4096);
    p.ray_tracing = in.value("ray_tracing", p.ray_tracing);

    const json lighting = group(in, "lighting");
    p.lighting[0] = readLighting(group(lighting, "desktop"), p.lighting[0]);
    p.lighting[1] = readLighting(group(lighting, "vr"), p.lighting[1]);
    p.probe_rays_per_frame = std::max(lighting.value("probe_rays_per_frame", p.probe_rays_per_frame), 0);
    p.rays_per_probe = lighting.value("rays_per_probe", p.rays_per_probe);
    p.far_plants_in_rays = lighting.value("far_plants_in_rays", p.far_plants_in_rays);
    p.far_plants_on_screen = lighting.value("far_plants_on_screen", p.far_plants_on_screen);

    const json textures = group(in, "textures");
    auto anisotropy = [&](const char* name, int& value) { value = std::clamp(textures.value(name, value), 1, 16); };
    anisotropy("grass_anisotropy", p.textures.grass_anisotropy);
    anisotropy("ground_anisotropy", p.textures.ground_anisotropy);
    anisotropy("vegetation_anisotropy", p.textures.vegetation_anisotropy);
    anisotropy("animal_anisotropy", p.textures.animal_anisotropy);

    p.relief = group(in, "ground").value("relief", p.relief);

    const json grass = group(in, "grass");
    p.grass_layers = grass.value("layers", p.grass_layers);
    p.grass_shadow = grass.value("shadow", p.grass_shadow);
    p.grass_see_through = grass.value("see_through", p.grass_see_through);

    const json vegetation = group(in, "vegetation");
    p.lod_bias = vegetation.value("lod_bias", p.lod_bias);
    p.normal_maps = vegetation.value("normal_maps", p.normal_maps);
    p.subsurface = vegetation.value("subsurface", p.subsurface);

    const json streaming = group(in, "streaming");
    auto a_frame = [&](const char* name, int& value) { value = std::max(streaming.value(name, value), 1); };
    a_frame("chunk_meshes_per_frame", p.streaming.chunk_meshes);
    a_frame("ray_chunks_per_frame", p.streaming.ray_chunks);
    a_frame("plant_volume_slices_per_frame", p.streaming.plant_volume_slices);

    return p;

}

}

std::vector<GraphicsPreset> dy::loadGraphicsPresets(const std::string& file) {

    std::vector<GraphicsPreset> presets;

    std::ifstream in(file);
    if(!in) {
        dy::log(dy::warning) << "Graphics : no " << file << ", the defaults\n";
        return presets;
    }

    try {
        // Comments are allowed
        const json root = json::parse(in, nullptr, true, true);
        for(const json& level : root.at("presets")) {
            GraphicsPreset preset = readPreset(level);
            if(preset.name.empty() || findGraphicsPreset(presets, preset.name) >= 0) {
                dy::log(dy::warning) << "Graphics : a level of " << file << " has no name of its own, left out\n";
                continue;
            }
            presets.push_back(std::move(preset));
        }
    } catch(const json::exception& e) {
        dy::log(dy::warning) << "Graphics : " << file << " : " << e.what() << ", the defaults\n";
        presets.clear();
    }

    return presets;

}

int dy::findGraphicsPreset(const std::vector<GraphicsPreset>& presets, const std::string& name) {
    for(size_t i = 0; i < presets.size(); i++) {
        if(presets[i].name == name) return (int) i;
    }
    return -1;
}

void dy::applyGraphicsPreset(const GraphicsPreset& preset, RenderSettings& settings) {
    settings.lighting[0] = preset.lighting[0];
    settings.lighting[1] = preset.lighting[1];
    settings.probe_rays_per_frame = preset.probe_rays_per_frame;
    settings.rays_per_probe = preset.rays_per_probe;
    settings.far_plants_in_rays = preset.far_plants_in_rays;
    settings.far_plants_on_screen = preset.far_plants_on_screen;
    settings.textures = preset.textures;
    settings.relief = preset.relief;
    settings.grass_params.layers = preset.grass_layers;
    settings.grass_params.shadow = preset.grass_shadow;
    settings.grass_params.silhouette = preset.grass_see_through;
    settings.vegetation_params.lod_bias = preset.lod_bias;
    settings.vegetation_params.normal_maps = preset.normal_maps;
    settings.vegetation_params.subsurface = preset.subsurface;
    settings.streaming = preset.streaming;
}
