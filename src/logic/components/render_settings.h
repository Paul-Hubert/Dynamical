#ifndef DY_RENDER_SETTINGS_H
#define DY_RENDER_SETTINGS_H

#include "logic/grass/grass_height.h"
#include "logic/grass/grass_species.h"
#include "logic/terrain/soil.h"

#include <glm/glm.hpp>

namespace dy {

// Context variable : runtime toggles of the scene passes, edited from the debug menu
struct RenderSettings {
    bool terrain = true;
    bool grass = true;
    bool grass_pass = false;    // set by GrassPass : this scene draws grass (the terrain is dark under it)
    bool models = true;
    bool vegetation = true;
    bool animals = true;
    float tree_shade = 0.85f;   // how dark it is under the plants (TreeShade), 0 : no shade

    // Ray traced lighting (docs/lighting.md) : what a target pays for, by tier. Nothing of it without the GPU's ray queries
    enum class LightingTier : int { Off, Low, Medium, High };
    struct Lighting {
        LightingTier tier = LightingTier::Medium;
        float traced_range = 60.f;          // metres : the sun is traced per pixel within, read from the probes beyond. 0 : probes only
        int sun_rays = 1;                   // a pixel
        float plant_geometry_range = 120.f; // metres : plants are their leaves in the rays within, a box beyond
        int shadowed_torches = 2;           // a pixel
        int animals = 32;                   // with a shadow

        static Lighting of(LightingTier tier) {
            switch(tier) {
                case LightingTier::Off: return { tier, 0.f, 0, 60.f, 0, 0 };
                case LightingTier::Low: return { tier, 25.f, 1, 60.f, 1, 8 };
                case LightingTier::Medium: return { tier, 60.f, 1, 120.f, 2, 32 };
                default: return { LightingTier::High, 150.f, 2, 250.f, 4, 128 };
            }
        }
    };
    Lighting lighting[2] = { Lighting::of(LightingTier::High), Lighting::of(LightingTier::Medium) };    // desktop, VR
    int probe_rays_per_frame = 250000;     // measured : a million is 20 ms a frame in a forest on an RTX 5080
    int rays_per_probe = 48;

    // Patches of mud, stones and sand in the ground, a map per chunk (SoilMaps, docs/ground.md) : no grass on them
    bool soil = true;
    SoilParams soil_params;
    float scree = 1.f;          // stones at the foot of the rock, where the ground gets steep : 0 none
    float relief = 1.f;         // depth of the relief of the mud, the stones and the sand, looked up in baked rays : 0 flat

    // Plants of the vegetation library (VegetationPass)
    struct Vegetation {
        float lod_bias = 2.f;       // > 1 keeps the detailed levels further away (Unity's QualitySettings.lodBias)
        int force_lod = -1;         // >= 0 : every plant drawn with this level (clamped to the species' last)
        bool normal_maps = true;
        bool subsurface = true;
        bool cull = true;           // plants out of the desktop camera's view are not drawn
    } vegetation_params;

    // Grass shells looked up in baked rays (grass.frag.glsl)
    struct Grass {
        glm::vec3 base_normal = glm::vec3(0.f, 1.f, 0.f);  // direction the blades grow in, before the wind
        float follow_terrain = 0.3f;    // how much they lean with the terrain normal
        float tile_size = 1.f;          // world size of the baked tile
        float height = 1.f;             // of the tallest blades, where the height map is at its highest
        GrassHeightParams height_map;   // octaves of Perlin noise baked in a map per chunk : where the grass is tall (docs/grass/11-height-map.md)
        float patch_amount = 0.45f;     // how much shorter the grass is where the height map is lowest
        float patch_size = 14.f;        // metres : three height patches of each layer in that (docs/grass/03-patches.md)
        float noise_frequency = 0.03f;  // in tiles : each layer bends its tiling with its own noise
        float noise_amplitude = 0.15f;
        float wind_strength = 0.12f;    // lean of the blades in the gusts
        float wind_speed = 1.f;
        float top_start = 0.f;          // part of the way from 67.5 degrees to the vertical where the top down rays blend in
        int layers = 2;                 // tall layers : the same tile turned, shifted and scaled, the closest hit wins
        float layer_spread = 1.f;       // 0 : every layer has the same tile size
        float layer_patch = 0.35f;      // height patches of each layer
        float undergrowth = 0.45f;      // height of the short dense layer under the tall ones, 0 : none
        float colour_variation = 0.5f;  // brightness and hue from blade to blade
        float dryness = 0.25f;          // part of the blades that are dry, in patches
        float curve = 0.7f;             // part of the wind that bends the blades instead of leaning them
        float shadow = 0.6f;            // blade on blade sun shadows
        float canopy = 0.6f;            // darker under the canopy
        float silhouette = 1.f;         // seen through where the ground falls away under the ray (crests, edges) : 0 never, more : deeper
        float azimuth_dither = 0.5f;    // towards one baked azimuth and elevation per pixel instead of two blended (1 : grainy)
        float baked_colour = 1.f;       // colours from the colour map of the rays (flowers, seeds), 0 : green by the height alone
        GrassSpeciesParams species;     // which kind of plant grows where, in patches (docs/grass/15-species.md)
    } grass_params;
};

}

#endif
