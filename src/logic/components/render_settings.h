#ifndef DY_RENDER_SETTINGS_H
#define DY_RENDER_SETTINGS_H

#include <glm/glm.hpp>

namespace dy {

// Context variable : runtime toggles of the scene passes, edited from the debug menu
struct RenderSettings {
    bool terrain = true;
    bool grass = true;
    bool grass_pass = false;    // set by GrassPass : this scene draws grass (the terrain is dark under it)
    bool models = true;
    bool vegetation = true;

    // Plants of the vegetation library (VegetationPass)
    struct Vegetation {
        float lod_bias = 2.f;       // > 1 keeps the detailed levels further away (Unity's QualitySettings.lodBias)
        int force_lod = -1;         // >= 0 : every plant drawn with this level (clamped to the species' last)
        bool normal_maps = true;
        bool subsurface = true;
    } vegetation_params;

    // Grass shells looked up in baked rays (grass.frag.glsl)
    struct Grass {
        glm::vec3 base_normal = glm::vec3(0.f, 1.f, 0.f);  // direction the blades grow in, before the wind
        float follow_terrain = 0.3f;    // how much they lean with the terrain normal
        float tile_size = 1.f;          // world size of the baked tile
        float height = 0.6f;            // of the tallest blades
        float patch_size = 14.f;        // metres : patches of taller and shorter grass (docs/grass/03-patches.md)
        float patch_amount = 0.45f;     // how much shorter the short patches are
        float noise_frequency = 0.03f;  // in tiles : each layer bends its tiling with its own noise
        float noise_amplitude = 0.15f;
        float wind_strength = 0.12f;    // lean of the blades in the gusts
        float wind_speed = 1.f;
        float top_start = 0.f;          // part of the way from 45 degrees to the vertical where the top down rays blend in
        int layers = 2;                 // tall layers : the same tile turned, shifted and scaled, the closest hit wins
        float layer_spread = 1.f;       // 0 : every layer has the same tile size
        float layer_patch = 0.35f;      // height patches of each layer
        float undergrowth = 0.45f;      // height of the short dense layer under the tall ones, 0 : none
        float colour_variation = 0.5f;  // brightness and hue from blade to blade
        float dryness = 0.25f;          // part of the blades that are dry, in patches
        float curve = 0.7f;             // part of the wind that bends the blades instead of leaning them
        float shadow = 0.6f;            // blade on blade sun shadows
        float canopy = 0.6f;            // darker under the canopy
        float silhouette = 0.75f;       // how much of the blades is cut on the crests
        float azimuth_dither = 0.5f;    // towards one baked azimuth per pixel instead of two blended (1 : grainy)
    } grass_params;
};

}

#endif
