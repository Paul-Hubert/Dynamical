#ifndef DY_GRAPHICS_H
#define DY_GRAPHICS_H

#include "logic/components/render_settings.h"
#include "logic/settings/settings.h"

#include <string>
#include <vector>

namespace dy {

// One level of resources/graphics.json (ultralow, low, medium, high, ultrahigh) : the values of what the time of
// a frame and the memory of the GPU depend on. A default one holds the defaults of the code, which "high" repeats
// (test GRAPHICS.2). docs/graphics.md
struct GraphicsPreset {
    std::string name;

    int view_distance = Settings::default_view_distance;    // Settings::view_distance
    bool ray_tracing = true;            // false : drawn as on a GPU without ray queries. Read when the device is made

    // RenderSettings, under the same names
    RenderSettings::Lighting lighting[2] = { RenderSettings().lighting[0], RenderSettings().lighting[1] };  // desktop, VR
    int probe_rays_per_frame = RenderSettings().probe_rays_per_frame;
    int rays_per_probe = RenderSettings().rays_per_probe;
    bool far_plants_in_rays = true;
    bool far_plants_on_screen = true;
    RenderSettings::Textures textures;      // read when the passes are made
    float relief = RenderSettings().relief;
    int grass_layers = RenderSettings::Grass().layers;
    float grass_shadow = RenderSettings::Grass().shadow;
    float grass_see_through = RenderSettings::Grass().silhouette;   // 0 : the shell is drawn once instead of three times
    float lod_bias = RenderSettings::Vegetation().lod_bias;
    bool normal_maps = true;
    bool subsurface = true;
    RenderSettings::Streaming streaming;

    bool operator==(const GraphicsPreset&) const = default;
};

// Context variable (dynamical) : the levels of the file, from the cheapest, and the one that was applied
struct Graphics {
    std::vector<GraphicsPreset> presets;
    int current = -1;   // none : the defaults of the code
};

constexpr const char* graphics_file = "./resources/graphics.json";

// Empty with a warning when the file is missing or is not what it should be. A value a level does not give is the
// default of the code
std::vector<GraphicsPreset> loadGraphicsPresets(const std::string& file = graphics_file);

// -1 : no level of that name
int findGraphicsPreset(const std::vector<GraphicsPreset>& presets, const std::string& name);

// All of the level that is in the RenderSettings. The textures only count before the passes are made
void applyGraphicsPreset(const GraphicsPreset& preset, RenderSettings& settings);

}

#endif
